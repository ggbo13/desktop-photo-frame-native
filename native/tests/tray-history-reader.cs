using System;
using System.Runtime.InteropServices;
using System.Security.Principal;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

public static class FrameTestTrayHistoryReader {
    [DllImport("ntdll.dll")] static extern int NtQuerySystemInformation(int type, IntPtr data, int length, out int result);
    [DllImport("ntdll.dll")] static extern int NtQueryKey(IntPtr handle, int type, IntPtr data, int length, out int result);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, int id);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] static extern bool DuplicateHandle(IntPtr source, IntPtr handle, IntPtr target, out IntPtr copy, uint access, bool inherit, uint options);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);

    // 仅读取同一用户的资源管理器托盘视图，避免普通 HKCU 查询受执行环境视图影响。
    public static RegistryKey OpenExplorerRoot(int processId) {
        string expected = "\\REGISTRY\\USER\\" + WindowsIdentity.GetCurrent().User.Value + "\\Control Panel\\NotifyIconSettings";
        int size = 1024 * 1024, used;
        IntPtr table;
        while (true) {
            table = Marshal.AllocHGlobal(size);
            int status = NtQuerySystemInformation(64, table, size, out used);
            if (status == 0) break;
            Marshal.FreeHGlobal(table);
            if (status != unchecked((int)0xc0000004) || size > 64 * 1024 * 1024)
                throw new Exception("无法读取实际托盘视图的句柄信息。");
            size = Math.Max(size * 2, used + 4096);
        }
        IntPtr process = OpenProcess(0x40, false, processId);
        IntPtr buffer = Marshal.AllocHGlobal(65536);
        try {
            if (process == IntPtr.Zero) return null;
            long count = Marshal.ReadInt64(table);
            for (long i = 0; i < count; i++) {
                IntPtr row = new IntPtr(table.ToInt64() + 16 + i * 40);
                if (Marshal.ReadInt64(row, 8) != processId) continue;
                IntPtr copy;
                // 副本仅申请 KEY_READ，不继承原句柄的写入权限。
                if (!DuplicateHandle(process, new IntPtr(Marshal.ReadInt64(row, 16)), GetCurrentProcess(), out copy, 0x20019, false, 0)) continue;
                bool retained = false;
                try {
                    int n;
                    if (NtQueryKey(copy, 3, buffer, 65536, out n) != 0) continue;
                    string name = Marshal.PtrToStringUni(IntPtr.Add(buffer, 4), Marshal.ReadInt32(buffer) / 2);
                    if (!name.Equals(expected, StringComparison.OrdinalIgnoreCase)) continue;
                    var key = RegistryKey.FromHandle(new SafeRegistryHandle(copy, true));
                    retained = true;
                    return key;
                } finally { if (!retained) CloseHandle(copy); }
            }
            return null;
        } finally {
            if (process != IntPtr.Zero) CloseHandle(process);
            Marshal.FreeHGlobal(buffer);
            Marshal.FreeHGlobal(table);
        }
    }
}
