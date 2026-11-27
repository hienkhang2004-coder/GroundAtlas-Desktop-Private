; GroundAtlas Desktop 2.2.2 — Windows x64 Single-File Installer (Inno Setup 6)
#define MyAppName "GroundAtlas Desktop"
#define MyAppVersion "2.2.2"
#define MyAppPublisher "GroundAtlas"
#define MyAppExeName "GroundAtlas.exe"

[Setup]
AppId={{9A3E5D8B-4B7C-4F92-B6E2-8C4E0B200001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..
OutputBaseFilename=GroundAtlas-Desktop-2.2.2-Setup
SetupIconFile=..\assets\groundatlas.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
WizardStyle=modern
DisableProgramGroupPage=auto
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
ChangesAssociations=yes
VersionInfoVersion=2.2.2.0
VersionInfoCompany=GroundAtlas
VersionInfoDescription=GroundAtlas Desktop 2.2.2 Setup (Native C++20 / OpenGL 3.3 / OSM Live / quyhoach.hanoi.vn)
VersionInfoProductName=GroundAtlas Desktop
VersionInfoProductVersion=2.2.2

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=Cài đặt [name/ver]
WelcomeLabel2=Trình cài đặt này sẽ cài [name/ver] (100% Native C++20 / OpenGL 3.3 / MapLibre Native) vào máy tính của bạn.%n%nỨng dụng chạy độc lập hoàn toàn, không cần cài thêm trình duyệt hay thư viện ngoài. Nhấn Next để tiếp tục.
FinishedHeadingLabel=Hoàn tất cài đặt [name]
FinishedLabelNoIcons=Đã cài đặt xong [name] trên máy tính của bạn.
FinishedLabel=Đã cài đặt xong [name] trên máy tính của bạn. Bạn có thể mở ứng dụng từ biểu tượng ngoài màn hình Desktop hoặc Start Menu.

[Tasks]
Name: "desktopicon"; Description: "Tạo biểu tượng trên màn hình Desktop (Create Desktop shortcut)"; GroupDescription: "Tùy chọn bổ sung:"
Name: "associate"; Description: "Mở file dự án .gatlas bằng GroundAtlas Desktop (Associate .gatlas files)"; GroupDescription: "Tùy chọn bổ sung:"

[Files]
Source: "..\portable-2.2.2\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{autoprograms}\{#MyAppName}\Gỡ cài đặt {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.gatlas"; ValueType: string; ValueName: ""; ValueData: "GroundAtlas.Project"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\GroundAtlas.Project"; ValueType: string; ValueName: ""; ValueData: "GroundAtlas Project"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\GroundAtlas.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\GroundAtlas.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Khởi chạy {#MyAppName} {#MyAppVersion} ngay bây giờ"; Flags: nowait postinstall skipifsilent
