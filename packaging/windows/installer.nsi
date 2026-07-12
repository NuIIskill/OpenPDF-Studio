; OpenPDF Studio — Windows installer (NSIS 3.x)
;
; Build via packaging/windows/package-win.sh — it stages the runtime files and
; passes the required defines:
;   makensis -DAPP_VERSION=0.1.0 -DSTAGE_DIR=..\..\dist\stage -DOUT_FILE=..\..\dist\OpenPDF-Studio-Setup.exe installer.nsi

Unicode true
SetCompressor /SOLID lzma

!ifndef APP_VERSION
  !define APP_VERSION "0.0.0"
!endif
!ifndef STAGE_DIR
  !error "STAGE_DIR not defined — run via package-win.sh"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "OpenPDF-Studio-Setup.exe"
!endif

!define APP_NAME      "OpenPDF Studio"
!define APP_EXE       "OpenPDFStudio.exe"
!define APP_PUBLISHER "nullskill"
!define APP_URL       "https://openpdf-studio.nullskill.de"
!define UNINST_KEY    "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenPDFStudio"

Name "${APP_NAME}"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "Software\OpenPDFStudio" "InstallDir"
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName"     "${APP_NAME}"
VIAddVersionKey "ProductVersion"  "${APP_VERSION}"
VIAddVersionKey "FileVersion"     "${APP_VERSION}"
VIAddVersionKey "FileDescription" "${APP_NAME} Installer"
VIAddVersionKey "LegalCopyright"  "${APP_PUBLISHER}"

; ── Modern UI ─────────────────────────────────────────────────────────────────
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define MUI_ICON   "${__FILEDIR__}\..\..\resources\windows\openpdf-studio.ico"
!define MUI_UNICON "${__FILEDIR__}\..\..\resources\windows\openpdf-studio.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${__FILEDIR__}\..\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "English"

; ── Install sections ──────────────────────────────────────────────────────────
Section "-Programm" SecApp
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Registry: install dir + Add/Remove-Programs entry
  WriteRegStr HKLM "Software\OpenPDFStudio" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName"     "${APP_NAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion"  "${APP_VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher"       "${APP_PUBLISHER}"
  WriteRegStr HKLM "${UNINST_KEY}" "URLInfoAbout"    "${APP_URL}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${APP_EXE}"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1

  ; EstimatedSize (KB) for Add/Remove Programs
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "$(SEC_STARTMENU)" SecStartMenu
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\$(LNK_UNINSTALL).lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "$(SEC_DESKTOP)" SecDesktop
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

; ── Uninstaller ───────────────────────────────────────────────────────────────
Section "Uninstall"
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\$(LNK_UNINSTALL).lnk"
  RMDir  "$SMPROGRAMS\${APP_NAME}"
  Delete "$DESKTOP\${APP_NAME}.lnk"

  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR"

  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "Software\OpenPDFStudio"
SectionEnd

; ── Localised strings ─────────────────────────────────────────────────────────
LangString SEC_STARTMENU ${LANG_GERMAN}  "Startmenü-Einträge"
LangString SEC_STARTMENU ${LANG_ENGLISH} "Start menu entries"
LangString SEC_DESKTOP   ${LANG_GERMAN}  "Desktop-Verknüpfung"
LangString SEC_DESKTOP   ${LANG_ENGLISH} "Desktop shortcut"
LangString LNK_UNINSTALL ${LANG_GERMAN}  "${APP_NAME} deinstallieren"
LangString LNK_UNINSTALL ${LANG_ENGLISH} "Uninstall ${APP_NAME}"

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "$(SEC_STARTMENU)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop}   "$(SEC_DESKTOP)"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "OpenPDF Studio benötigt 64-Bit-Windows. / OpenPDF Studio requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd
