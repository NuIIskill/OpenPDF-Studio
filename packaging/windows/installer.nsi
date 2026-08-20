; OpenPDF Studio — Windows installer (NSIS 3.x)
;
; Build via packaging/windows/package-win.sh — it stages the runtime files and
; passes the required defines:
;   makensis -DAPP_VERSION=0.1.0 -DSTAGE_DIR=..\..\dist\stage -DOUT_FILE=..\..\dist\OpenPDF-Studio-Setup.exe installer.nsi
;
; Optional: -DGPL_ONLY=1 — gesetzt, wenn der Build gegen Poppler-Qt6 gelinkt ist.
; Ein solches Binary darf nur unter der GPL verteilt werden (LICENSES/README.md);
; ohne das Define gilt die Doppellizenz des Quelltextes.
;
; Laufzeit-Parameter des fertigen Setups:
;   Setup.exe /S /KEY=XXXX-XXXX   stille Installation, hinterlegt einen
;                                 Business-Schlüssel maschinenweit unter
;                                 HKLM\Software\OpenPDFStudio\BusinessLicense
; Das ist reine Ausrollhilfe für Administratoren, keine Prüfung: der Installer
; validiert den Schlüssel nicht und hängt nichts davon ab. Ohne /KEY bleibt ein
; bereits hinterlegter Schlüssel unangetastet. Personal Use braucht ihn nie
; (LICENSES/OPENPDF-BUSINESS.txt, Abschnitt 2).

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
!define APP_SOURCE    "https://github.com/NuIIskill/OpenPDF-Studio"
!define APP_COPYRIGHT "Copyright (C) 2026 nullskill"
!define UNINST_KEY    "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenPDFStudio"

!ifdef GPL_ONLY
  !define APP_SPDX "GPL-3.0-only"
!else
  !define APP_SPDX "GPL-3.0-only OR LicenseRef-OpenPDF-Commercial"
!endif

Var BusinessKey
Var UsageMode
Var UsageDlg
Var RbPersonal
Var RbBusiness
Var KeyLabel
Var KeyField

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
VIAddVersionKey "CompanyName"     "${APP_PUBLISHER}"
VIAddVersionKey "LegalCopyright"  "${APP_COPYRIGHT}"
VIAddVersionKey "Comments"        "${APP_SPDX} - ${APP_SOURCE}"

; ── Modern UI ─────────────────────────────────────────────────────────────────
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"

!define MUI_ICON   "${__FILEDIR__}\..\..\resources\windows\openpdf-studio.ico"
!define MUI_UNICON "${__FILEDIR__}\..\..\resources\windows\openpdf-studio.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME

; Die GPL verlangt keine Zustimmung zum Benutzen des Programms (Abschnitt 9),
; deshalb ein "Weiter" statt "Ich stimme zu". Angezeigt wird die Übersicht
; LICENSE; die Volltexte landen mit dem Staging unter LICENSES\.
!define MUI_LICENSEPAGE_TEXT_TOP    "$(LIC_TOP)"
!define MUI_LICENSEPAGE_TEXT_BOTTOM "$(LIC_BOTTOM)"
!define MUI_LICENSEPAGE_BUTTON      "$(^NextBtn)"
!insertmacro MUI_PAGE_LICENSE "${__FILEDIR__}\..\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
Page custom UsagePageCreate UsagePageLeave
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "English"

; ── Seite: Art der Nutzung ────────────────────────────────────────────────────
; Kurze Selbstauskunft, mehr nicht. Sie schaltet nichts frei und sperrt nichts
; (LICENSES/OPENPDF-BUSINESS.txt, Abschnitt 6); wer "Privat" wählt, bekommt vom
; Thema Lizenzierung nie wieder etwas zu sehen. Bei /S wird die Seite nicht
; angezeigt — dann gilt "business", falls /KEY= mitkam, sonst "personal".
Function UsageSyncFields
  ${NSD_GetState} $RbBusiness $1
  ${If} $1 == ${BST_CHECKED}
    EnableWindow $KeyLabel 1
    EnableWindow $KeyField 1
  ${Else}
    EnableWindow $KeyLabel 0
    EnableWindow $KeyField 0
  ${EndIf}
FunctionEnd

Function UsageOnPick
  Pop $0          ; HWND des angeklickten Radiobuttons
  Call UsageSyncFields
FunctionEnd

Function UsagePageCreate
  !insertmacro MUI_HEADER_TEXT "$(PAGE_USAGE_TITLE)" "$(PAGE_USAGE_SUB)"
  nsDialogs::Create 1018
  Pop $UsageDlg
  ${If} $UsageDlg == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 0 100% 26u "$(USAGE_INTRO)"
  Pop $0
  ${NSD_CreateRadioButton} 0 32u 100% 12u "$(USAGE_PERSONAL)"
  Pop $RbPersonal
  ${NSD_CreateRadioButton} 0 46u 100% 12u "$(USAGE_BUSINESS)"
  Pop $RbBusiness
  ${NSD_OnClick} $RbPersonal UsageOnPick
  ${NSD_OnClick} $RbBusiness UsageOnPick

  ${NSD_CreateLabel} 10u 62u 90% 10u "$(USAGE_KEY_LABEL)"
  Pop $KeyLabel
  ${NSD_CreateText} 10u 73u 70% 12u "$BusinessKey"
  Pop $KeyField

  ${NSD_CreateLabel} 0 92u 100% 26u "$(USAGE_FOOT)"
  Pop $0

  ${If} $UsageMode == "business"
    ${NSD_SetState} $RbBusiness ${BST_CHECKED}
  ${Else}
    ${NSD_SetState} $RbPersonal ${BST_CHECKED}
  ${EndIf}
  Call UsageSyncFields

  nsDialogs::Show
FunctionEnd

Function UsagePageLeave
  ${NSD_GetState} $RbBusiness $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $UsageMode "business"
    ${NSD_GetText} $KeyField $BusinessKey
  ${Else}
    ; Privat heißt: nichts hinterlassen, auch kein per /KEY mitgegebener Wert.
    StrCpy $UsageMode "personal"
    StrCpy $BusinessKey ""
  ${EndIf}
FunctionEnd

; ── Install sections ──────────────────────────────────────────────────────────
Section "-Programm" SecApp
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Registry: install dir + Add/Remove-Programs entry
  WriteRegStr HKLM "Software\OpenPDFStudio" "InstallDir" "$INSTDIR"

  ; Selbstauskunft aus der Nutzungsseite. Reine Angabe: die Anwendung entscheidet
  ; daran nur, ob sie den Business-Hinweis überhaupt zeigt — nie, was sie kann.
  WriteRegStr HKLM "Software\OpenPDFStudio" "Usage" "$UsageMode"

  ; Business-Schlüssel aus /KEY= (Massenverteilung). Maschinenweit, damit ihn
  ; jeder Benutzer des Rechners sieht; die Anwendung liest HKLM vor HKCU.
  ; Ohne /KEY wird hier nichts geschrieben und nichts gelöscht — ein früher
  ; ausgerollter Schlüssel überlebt damit die Aktualisierung.
  ${If} $BusinessKey != ""
    ${GetTime} "" "L" $R2 $R3 $R4 $R5 $R6 $R7 $R8
    WriteRegStr HKLM "Software\OpenPDFStudio\BusinessLicense" "Key"    "$BusinessKey"
    WriteRegStr HKLM "Software\OpenPDFStudio\BusinessLicense" "Source" "installer ${APP_VERSION}"
    WriteRegStr HKLM "Software\OpenPDFStudio\BusinessLicense" "SetAt"  "$R4-$R3-$R2"
    DetailPrint "$(MSG_KEY_STORED)"
  ${EndIf}
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName"     "${APP_NAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion"  "${APP_VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher"       "${APP_PUBLISHER}"
  WriteRegStr HKLM "${UNINST_KEY}" "URLInfoAbout"    "${APP_URL}"
  WriteRegStr HKLM "${UNINST_KEY}" "HelpLink"        "${APP_SOURCE}"
  WriteRegStr HKLM "${UNINST_KEY}" "URLUpdateInfo"   "${APP_SOURCE}/releases"
  WriteRegStr HKLM "${UNINST_KEY}" "Comments"        "${APP_SPDX} - ${APP_SOURCE}"
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
  ; Nimmt den Unterschlüssel BusinessLicense mit — eine Deinstallation
  ; hinterlässt keinen Schlüssel auf dem Rechner. Beim erneuten Ausrollen
  ; also wieder /KEY= mitgeben.
  DeleteRegKey HKLM "Software\OpenPDFStudio"
SectionEnd

; ── Localised strings ─────────────────────────────────────────────────────────
LangString SEC_STARTMENU ${LANG_GERMAN}  "Startmenü-Einträge"
LangString SEC_STARTMENU ${LANG_ENGLISH} "Start menu entries"
LangString SEC_DESKTOP   ${LANG_GERMAN}  "Desktop-Verknüpfung"
LangString SEC_DESKTOP   ${LANG_ENGLISH} "Desktop shortcut"
LangString LNK_UNINSTALL ${LANG_GERMAN}  "${APP_NAME} deinstallieren"
LangString LNK_UNINSTALL ${LANG_ENGLISH} "Uninstall ${APP_NAME}"

!ifdef GPL_ONLY
LangString LIC_TOP ${LANG_GERMAN}  "Diese Fassung ist gegen Poppler-Qt6 gelinkt und wird unter der GNU General Public License v3 verteilt. Zum Benutzen ist keine Zustimmung nötig."
LangString LIC_TOP ${LANG_ENGLISH} "This build is linked against Poppler-Qt6 and is distributed under the GNU General Public License v3. No agreement is required to use it."
!else
LangString LIC_TOP ${LANG_GERMAN}  "OpenPDF Studio steht wahlweise unter der GNU General Public License v3 oder der OpenPDF Studio Commercial License."
LangString LIC_TOP ${LANG_ENGLISH} "OpenPDF Studio is available under either the GNU General Public License v3 or the OpenPDF Studio Commercial License."
!endif

LangString PAGE_USAGE_TITLE ${LANG_GERMAN}  "Art der Nutzung"
LangString PAGE_USAGE_TITLE ${LANG_ENGLISH} "Type of use"
LangString PAGE_USAGE_SUB   ${LANG_GERMAN}  "Eine Selbstauskunft — sie ändert keine Rechte und schaltet nichts frei."
LangString PAGE_USAGE_SUB   ${LANG_ENGLISH} "Self-declaration only - it changes no rights and unlocks nothing."

LangString USAGE_INTRO    ${LANG_GERMAN}  "OpenPDF Studio ist privat vollständig und dauerhaft kostenlos. Nur die Medienwiedergabe braucht bei geschäftlicher Nutzung nach 30 Tagen eine Business-Lizenz."
LangString USAGE_INTRO    ${LANG_ENGLISH} "OpenPDF Studio is completely and permanently free for personal use. Only media playback needs a Business License after 30 days of business use."
LangString USAGE_PERSONAL ${LANG_GERMAN}  "Privat — nichts weiter erforderlich"
LangString USAGE_PERSONAL ${LANG_ENGLISH} "Personal - nothing further required"
LangString USAGE_BUSINESS ${LANG_GERMAN}  "Geschäftlich (Firma, Freiberuf, Behörde, Verein)"
LangString USAGE_BUSINESS ${LANG_ENGLISH} "Business (company, freelance, public body, non-profit)"
LangString USAGE_KEY_LABEL ${LANG_GERMAN}  "Business-Schlüssel, falls vorhanden — sonst leer lassen:"
LangString USAGE_KEY_LABEL ${LANG_ENGLISH} "Business key, if you have one - otherwise leave empty:"
LangString USAGE_FOOT     ${LANG_GERMAN}  "Die Angabe entscheidet nur, ob das Programm den Lizenzhinweis zeigt. Es funktioniert in beiden Fällen vollständig gleich, mit und ohne Schlüssel."
LangString USAGE_FOOT     ${LANG_ENGLISH} "The answer only decides whether the program shows the licensing notice. It works exactly the same either way, with or without a key."

LangString MSG_KEY_STORED ${LANG_GERMAN}  "Business-Schlüssel hinterlegt (HKLM\Software\OpenPDFStudio\BusinessLicense)"
LangString MSG_KEY_STORED ${LANG_ENGLISH} "Business key stored (HKLM\Software\OpenPDFStudio\BusinessLicense)"

LangString LIC_BOTTOM ${LANG_GERMAN}  "Volltexte: Ordner LICENSES. Quelltext: ${APP_SOURCE}"
LangString LIC_BOTTOM ${LANG_ENGLISH} "Full texts: LICENSES folder. Source code: ${APP_SOURCE}"

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

  ; /KEY=... einsammeln; fehlt der Parameter, bleibt $BusinessKey leer.
  StrCpy $UsageMode "personal"
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/KEY=" $R1
  ${IfNot} ${Errors}
    StrCpy $BusinessKey $R1
    ${If} $BusinessKey != ""
      StrCpy $UsageMode "business"
    ${EndIf}
  ${EndIf}
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd
