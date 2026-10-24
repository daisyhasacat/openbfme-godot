Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
Name "OpenBFME"
OutFile "${OUTPUT}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\OpenBFME"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "English"
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "OpenBFME requires Windows x64."
    Abort
  ${EndIf}
FunctionEnd

Section "OpenBFME and game content"
  AddSize ${INSTALL_KB}
  InitPluginsDir
  File /oname=$PLUGINSDIR\Install.ps1 "Install.ps1"
  DetailPrint "Verifying and extracting game content. This can take several minutes."
  ClearErrors
  ${DisableX64FSRedirection}
  ExecWait '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\Install.ps1" -Payload "$EXEDIR\OpenBFME-content.tar" -Destination "$INSTDIR" -ExpectedHash "${PAYLOAD_SHA256}"' $0
  ${EnableX64FSRedirection}
  ${If} ${Errors}
    MessageBox MB_ICONSTOP "Could not start Windows PowerShell to install the content."
    SetErrorLevel 1
    Abort
  ${EndIf}
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Installation failed. Keep OpenBFME-content.tar beside this installer, and choose an empty folder with enough space. Exit code: $0"
    SetErrorLevel 1
    Abort
  ${EndIf}
  CreateShortcut "$DESKTOP\OpenBFME.lnk" "$INSTDIR\OpenBFME.exe" "" "$INSTDIR\OpenBFME.exe"
  CreateDirectory "$SMPROGRAMS\OpenBFME"
  CreateShortcut "$SMPROGRAMS\OpenBFME\OpenBFME.lnk" "$INSTDIR\OpenBFME.exe" "" "$INSTDIR\OpenBFME.exe"
SectionEnd
