; MusicPlayer2-gw 安装包脚本
; 用 Inno Setup 6 编译：ISCC.exe installer.iss

[Setup]
AppName=MusicPlayer2
AppVersion=1.0.0
AppPublisher=gw
//AppPublisherURL=https://github.com/1497105876
DefaultDirName={autopf}\MusicPlayer2
DefaultGroupName=MusicPlayer2
UninstallDisplayIcon={app}\MusicPlayer2.exe
OutputDir=installer_output
OutputBaseFilename=MusicPlayer2-gw-setup-1.1.0
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
LicenseFile=
SetupIconFile=MusicPlayer2\res\MusicPlayer2.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加选项:"; Flags: checkedonce

[Files]
; 主程序
Source: "x64\Release\MusicPlayer2.exe"; DestDir: "{app}"; Flags: ignoreversion

; 核心 DLL
Source: "x64\Debug\bass.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "x64\Debug\bass_fx.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "x64\Release\tag.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "x64\Release\SciLexer.dll"; DestDir: "{app}"; Flags: ignoreversion

; BASS 编码器
Source: "MusicPlayer2\Encoder\bassenc.dll"; DestDir: "{app}\Encoder"; Flags: ignoreversion
Source: "MusicPlayer2\Encoder\bassmix.dll"; DestDir: "{app}\Encoder"; Flags: ignoreversion
Source: "MusicPlayer2\Encoder\flac.exe"; DestDir: "{app}\Encoder"; Flags: ignoreversion
Source: "MusicPlayer2\Encoder\lame.exe"; DestDir: "{app}\Encoder"; Flags: ignoreversion
Source: "MusicPlayer2\Encoder\oggenc.exe"; DestDir: "{app}\Encoder"; Flags: ignoreversion

; BASS 插件
Source: "MusicPlayer2\Plugins\basscd.dll"; DestDir: "{app}\Plugins"; Flags: ignoreversion
Source: "MusicPlayer2\Plugins\bassflac.dll"; DestDir: "{app}\Plugins"; Flags: ignoreversion
Source: "MusicPlayer2\Plugins\bassmidi.dll"; DestDir: "{app}\Plugins"; Flags: ignoreversion
Source: "MusicPlayer2\Plugins\basswma.dll"; DestDir: "{app}\Plugins"; Flags: ignoreversion
Source: "MusicPlayer2\Plugins\bass_aac.dll"; DestDir: "{app}\Plugins"; Flags: ignoreversion
Source: "MusicPlayer2\Plugins\bass_ape.dll"; DestDir: "{app}\Plugins"; Flags: ignoreversion

; 语言文件
Source: "MusicPlayer2\language\English.ini"; DestDir: "{app}\language"; Flags: ignoreversion
Source: "MusicPlayer2\language\Russian.ini"; DestDir: "{app}\language"; Flags: ignoreversion
Source: "MusicPlayer2\language\Simplified_Chinese.ini"; DestDir: "{app}\language"; Flags: ignoreversion
Source: "MusicPlayer2\language\Traditional_Chinese.ini"; DestDir: "{app}\language"; Flags: ignoreversion

; 皮肤 XML
Source: "MusicPlayer2\skins\*.xml"; DestDir: "{app}\skins"; Flags: ignoreversion
Source: "MusicPlayer2\skins\*.xsd"; DestDir: "{app}\skins"; Flags: ignoreversion
Source: "MusicPlayer2\skins\miniMode\*.xml"; DestDir: "{app}\skins\miniMode"; Flags: ignoreversion
Source: "MusicPlayer2\skins\panels\*.xml"; DestDir: "{app}\skins\panels"; Flags: ignoreversion

; 资源文件（图标等）
Source: "MusicPlayer2\res\*.ico"; DestDir: "{app}\res"; Flags: ignoreversion
Source: "MusicPlayer2\res\*.bmp"; DestDir: "{app}\res"; Flags: ignoreversion
Source: "MusicPlayer2\res\*.cur"; DestDir: "{app}\res"; Flags: ignoreversion
Source: "MusicPlayer2\res\*.png"; DestDir: "{app}\res"; Flags: ignoreversion
Source: "MusicPlayer2\res\*.txt"; DestDir: "{app}\res"; Flags: ignoreversion
Source: "MusicPlayer2\res\hzpy-utf8.txt"; DestDir: "{app}\res"; Flags: ignoreversion

[Icons]
Name: "{group}\MusicPlayer2"; Filename: "{app}\MusicPlayer2.exe"
Name: "{group}\卸载 MusicPlayer2"; Filename: "{uninstallexe}"
Name: "{commondesktop}\MusicPlayer2"; Filename: "{app}\MusicPlayer2.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\MusicPlayer2.exe"; Description: "立即启动 MusicPlayer2"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
