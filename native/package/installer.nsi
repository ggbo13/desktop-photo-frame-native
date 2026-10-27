Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!ifndef APP_DIR
 !error "需要指定 APP_DIR"
!endif
!ifndef PACKAGE_OUTPUT
 !error "需要指定 PACKAGE_OUTPUT"
!endif
Name "桌面相框原生版 2.0.0-preview.20"
OutFile "${PACKAGE_OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\DesktopPhotoFrameNative"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Icon "frame.ico"
UninstallIcon "frame.ico"
VIProductVersion "2.0.0.20"
VIAddVersionKey /LANG=2052 "ProductName" "桌面相框原生版"
VIAddVersionKey /LANG=2052 "FileDescription" "桌面相框原生预览版安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "2.0.0-preview.20"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Desktop Photo Frame"
Var TestInstall
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "安装原生相框预览版，支持播放列表、自适应相框、照片管理和系统托盘。$\r$\n$\r$\n旧版程序及照片保持不变；原生版配置单独保存。$\r$\n$\r$\n此版本仍在进行最终性能和多显示器验收。"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\DesktopPhotoFrameNative.exe"
!define MUI_FINISHPAGE_RUN_TEXT "打开桌面相框原生版"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

Function .onInit
 ${GetParameters} $0
 ClearErrors
 ${GetOptions} $0 "/TEST" $1
 ${IfNot} ${Errors}
  StrCpy $TestInstall "1"
 ${EndIf}
FunctionEnd

Section "桌面相框"
 SetOutPath "$INSTDIR"
 File "${APP_DIR}\DesktopPhotoFrameNative.exe"
 File "${APP_DIR}\使用说明.txt"
 File /r "${APP_DIR}\licenses"
 WriteUninstaller "$INSTDIR\卸载桌面相框原生版.exe"
 ${If} $TestInstall != "1"
  CreateShortcut "$DESKTOP\桌面相框原生版.lnk" "$INSTDIR\DesktopPhotoFrameNative.exe"
  CreateShortcut "$SMPROGRAMS\桌面相框原生版.lnk" "$INSTDIR\DesktopPhotoFrameNative.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopPhotoFrameNative" "DisplayName" "桌面相框原生版"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopPhotoFrameNative" "DisplayVersion" "2.0.0-preview.20"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopPhotoFrameNative" "UninstallString" '"$INSTDIR\卸载桌面相框原生版.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopPhotoFrameNative" "DisplayIcon" "$INSTDIR\DesktopPhotoFrameNative.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopPhotoFrameNative" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopPhotoFrameNative" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopPhotoFrameNative" "NoRepair" 1
 ${EndIf}
SectionEnd

Function un.onInit
 MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "卸载将移除原生版程序、快捷方式和原生版自启项。照片、旧版和相框配置会保留。是否继续？" IDYES confirmed
 Abort
 confirmed:
FunctionEnd
Section "Uninstall"
 Delete "$INSTDIR\DesktopPhotoFrameNative.exe"
 Delete "$INSTDIR\使用说明.txt"
 Delete "$INSTDIR\licenses\json-MIT.txt"
 Delete "$INSTDIR\licenses\webp-COPYING.txt"
 Delete "$INSTDIR\licenses\webp-PATENTS.txt"
 Delete "$INSTDIR\licenses\avif-LICENSE.txt"
 Delete "$INSTDIR\licenses\dav1d-COPYING.txt"
 Delete "$INSTDIR\licenses\libyuv-LICENSE.txt"
 Delete "$INSTDIR\licenses\llvm-mingw-LICENSE.txt"
 RMDir "$INSTDIR\licenses"
 Delete "$DESKTOP\桌面相框原生版.lnk"
 Delete "$SMPROGRAMS\桌面相框原生版.lnk"
 DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "DesktopPhotoFrameNative"
 DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopPhotoFrameNative"
 Delete "$INSTDIR\卸载桌面相框原生版.exe"
 RMDir "$INSTDIR"
SectionEnd
