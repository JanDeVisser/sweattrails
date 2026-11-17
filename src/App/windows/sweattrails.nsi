; The Windows installer. It installs SweatTrails for the user who runs it,
; in %LOCALAPPDATA%\Programs\SweatTrails, without administrator rights,
; with a Start menu shortcut and an uninstaller, which Windows lists under
; installed apps. See specs/63-packaging.md. scripts/package-windows.sh
; runs it:
;
;   makensis -DVERSION=1.0.0 -DAPP=<dir> -DOUTFILE=<setup.exe> sweattrails.nsi
;
; APP has sweattrails.exe and share\. Paths read while making the
; installer are the Mac's, with /; paths on Windows have \.

Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!define NAME "SweatTrails"
!define PUBLISHER "Jan de Visser"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${NAME}"

Name "${NAME}"
OutFile "${OUTFILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\${NAME}"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
SetCompressor /SOLID lzma
ManifestDPIAware true

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${NAME}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${NAME} installer"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026, ${PUBLISHER}"

!define MUI_ICON "${APP}/share/icons/sweattrails.ico"
!define MUI_UNICON "${APP}/share/icons/sweattrails.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\sweattrails.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${NAME}"

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
    SetShellVarContext current
FunctionEnd

Function un.onInit
    SetShellVarContext current
FunctionEnd

Section
    ; Always a folder of its own, whatever folder the user chose: the
    ; installer replaces share\ in it, and the uninstaller removes it.
    ${GetFileName} "$INSTDIR" $0
    ${If} $0 != "${NAME}"
        StrCpy $INSTDIR "$INSTDIR\${NAME}"
    ${EndIf}
    SetOutPath "$INSTDIR"
    ; A new version's share\ replaces the old one, so what it dropped goes.
    RMDir /r "$INSTDIR\share"
    File "${APP}/sweattrails.exe"
    File /r "${APP}/share"
    WriteUninstaller "$INSTDIR\uninstall.exe"
    CreateShortcut "$SMPROGRAMS\${NAME}.lnk" "$INSTDIR\sweattrails.exe"

    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${NAME}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\sweattrails.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" $0
SectionEnd

; Only what the installer put there, in the folder it made for the app.
; The activities and settings, in
; %APPDATA%\sweattrails and %LOCALAPPDATA%\sweattrails, stay.
Section "Uninstall"
    Delete "$SMPROGRAMS\${NAME}.lnk"
    Delete "$INSTDIR\sweattrails.exe"
    RMDir /r "$INSTDIR\share"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
SectionEnd
