# 原生相框测试与托盘隔离

当前构建：`2.0.0-preview.14`。

preview.14 默认回归为 6 组，其中 `composition-idle-smoke.ps1` 无需截图，检查空闲提交、唤醒频率及画布面积。较长的几何循环可以单独运行 `pwsh -NoProfile -File native/tests/composition-envelope-smoke.ps1`，默认四角和四种效果共 208 次切换；它不等于录屏黑闪或硬件呈现掉帧验收。

先构建，再运行统一回归：

```powershell
pwsh -NoProfile -File native/build-app.ps1 -OutputDirectory native/out/2.0.0-preview.14-delivery
pwsh -NoProfile -File native/tests/run-regression.ps1
```

安装包验证：

```powershell
pwsh -NoProfile -File native/build-installer.ps1
pwsh -NoProfile -File native/tests/installer-smoke.ps1
```

## 隔离规则

- 原生应用测试必须通过 `frame-test-runtime.ps1` 的 `Start-FrameTest` 启动。它只接受工作区 `native` 内的程序副本，检查程序版本，并强制添加 `--no-tray`。
- preview.13 之前的二进制不支持托盘隔离，启动器会在运行前拦截。对照旧版本应使用独立 Windows 测试账户。
- 程序自身也在 `--test-seconds`、`--test-hooks`、`--stress` 和故障注入模式下关闭托盘登记；软件渲染恢复子进程继承该状态。临时状态不保存到用户配置。
- 普通启动保留托盘。不要把候选构建当作测试脚本手工双击运行；普通启动会按正常应用登记托盘图标。
- `tray-controls-smoke.ps1` 验证托盘回调、菜单和命令逻辑，不登记实时图标。`tray-isolation-smoke.ps1` 直接检查 Shell 中不存在测试图标，并比较运行前后的历史登记。
- 真实图标登记验证和旧 Electron 性能对照需要在独立 Windows 测试账户中显式传入 `-IsolatedWindowsProfile`；当前桌面的默认回归不会执行它们。
- `run-regression.ps1` 最后检查工作区测试程序的托盘历史，发现残留就失败。它不会擅自删除任何注册表记录。
- 历史检查使用同一用户、同一桌面会话中资源管理器实际打开的注册表视图，复制句柄仅有读取权限。无法读取时明确失败，不回退到可能被执行环境隔离的普通 HKCU 查询；清理验收还必须关闭并重开设置页，核验真实界面。

单独验证无托盘模式及旧二进制拦截：

```powershell
pwsh -NoProfile -File native/tests/tray-isolation-smoke.ps1
pwsh -NoProfile -File native/tests/test-runtime-smoke.ps1
```

原始性能样机没有系统托盘，相关样机测试保留原有启动方式。旧代码、旧安装包和测试产物继续保留。
