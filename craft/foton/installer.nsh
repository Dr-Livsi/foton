; SPDX-License-Identifier: GPL-2.0-or-later
;
; Included into the NSIS installer script of KDE Craft.
;
; Before installing, the Craft installer offers to kill every process started from
; the installation folder. Installing directly into a folder with other programs
; (e.g. C:\Program Files) would therefore stop them, and the uninstaller would
; remove files from that folder. Only allow a new or empty folder or the folder
; of an earlier Foton installation.

!define MUI_DIRECTORYPAGE_TEXT_TOP "Foton has to be installed into its own folder. Choose a new or empty folder, or the folder of an earlier Foton installation."

Function .onVerifyInstDir
    IfFileExists "$INSTDIR\bin\foton.exe" accept
    IfFileExists "$INSTDIR\*.*" 0 accept
    FindFirst $0 $1 "$INSTDIR\*.*"
    loop:
        StrCmp $1 "" empty
        StrCmp $1 "." next
        StrCmp $1 ".." next
        FindClose $0
        ; Disables the Next button.
        Abort
    next:
        FindNext $0 $1
        Goto loop
    empty:
    FindClose $0
    accept:
FunctionEnd
