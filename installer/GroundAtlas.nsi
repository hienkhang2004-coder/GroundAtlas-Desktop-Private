; GroundAtlas Desktop 2.3.0 — Commercial Windows x64 Single-File Installer (NSIS 3, zlib/BSD commercial-free)
Unicode True
ManifestDPIAware true
SetCompressor /SOLID lzma

!define APP_NAME "GroundAtlas Desktop"
!define APP_VERSION "2.3.2"
!define APP_VERSION_FOUR "2.3.2.0"
!define APP_PUBLISHER "GroundAtlas"
!define APP_EXE "GroundAtlas.exe"
!define APP_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\GroundAtlasDesktop"
!define APP_REG_KEY "Software\GroundAtlas\Desktop"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "..\GroundAtlas-Desktop-${APP_VERSION}-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"
InstallDirRegKey HKCU "${APP_REG_KEY}" "InstallDir"
RequestExecutionLevel user
ShowInstDetails show
ShowUnInstDetails show

VIProductVersion "${APP_VERSION_FOUR}"
VIAddVersionKey /LANG=1033 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1033 "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey /LANG=1033 "FileDescription" "${APP_NAME} ${APP_VERSION} Setup (Pure Native C++20 / OpenGL 3.3 / Per-Seat License)"
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright (c) 2026 GroundAtlas"

!include "MUI2.nsh"
!include "FileFunc.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON "..\assets\groundatlas.ico"
!define MUI_UNICON "..\assets\groundatlas.ico"

!define MUI_WELCOMEPAGE_TITLE "Cài đặt ${APP_NAME} ${APP_VERSION}"
!define MUI_WELCOMEPAGE_TEXT "Trình cài đặt này sẽ cài ${APP_NAME} ${APP_VERSION} (100% Native C++20 / OpenGL 3.3 / MapLibre Native) vào máy tính của bạn.$\r$\n$\r$\nỨng dụng chạy độc lập hoàn toàn, không cần trình duyệt hay thư viện ngoài. Hỗ trợ Free Edition (xem & nạp OSM không giới hạn) và Pro 2.x Per-Seat Lifetime.$\r$\n$\r$\nNhấn Next để tiếp tục."

!define MUI_LICENSEPAGE_TEXT_TOP "Thông tin bản quyền thương mại & giấy phép thư viện nguồn mở (Qt 6 LGPLv3, MapLibre BSD-2, Monocypher CC0):"
!define MUI_FINISHPAGE_TITLE "Hoàn tất cài đặt ${APP_NAME} ${APP_VERSION}"
!define MUI_FINISHPAGE_TEXT "${APP_NAME} ${APP_VERSION} đã được cài đặt thành công trên máy tính của bạn.$\r$\n$\r$\nBạn có thể mở ứng dụng từ biểu tượng trên Desktop hoặc Start Menu."
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Khởi chạy ${APP_NAME} ${APP_VERSION} ngay bây giờ"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSES.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "${APP_NAME} ${APP_VERSION}" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "..\portable-${APP_VERSION}\*.*"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Install location & Add/Remove Programs metadata (per-user HKCU)
  WriteRegStr HKCU "${APP_REG_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${APP_REG_KEY}" "Version" "${APP_VERSION}"

  WriteRegStr HKCU "${APP_UNINST_KEY}" "DisplayName" "${APP_NAME} ${APP_VERSION}"
  WriteRegStr HKCU "${APP_UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${APP_UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKCU "${APP_UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE},0"
  WriteRegStr HKCU "${APP_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${APP_UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${APP_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${APP_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${APP_UNINST_KEY}" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${APP_UNINST_KEY}" "EstimatedSize" "$0"

  ; Start Menu & Desktop shortcuts
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\Gỡ cài đặt ${APP_NAME}.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
  CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0

  ; Associate .gatlas project files (per-user HKCU\Software\Classes)
  WriteRegStr HKCU "Software\Classes\.gatlas" "" "GroundAtlas.Project"
  WriteRegStr HKCU "Software\Classes\GroundAtlas.Project" "" "GroundAtlas Project"
  WriteRegStr HKCU "Software\Classes\GroundAtlas.Project\DefaultIcon" "" "$INSTDIR\${APP_EXE},0"
  WriteRegStr HKCU "Software\Classes\GroundAtlas.Project\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'

  System::Call 'shell32.dll::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\${APP_NAME}.lnk"
  RMDir /r "$SMPROGRAMS\${APP_NAME}"

  DeleteRegKey HKCU "${APP_UNINST_KEY}"
  DeleteRegKey HKCU "${APP_REG_KEY}"
  DeleteRegValue HKCU "Software\Classes\.gatlas" ""
  DeleteRegKey HKCU "Software\Classes\GroundAtlas.Project"

  RMDir /r "$INSTDIR"

  System::Call 'shell32.dll::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
