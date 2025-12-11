
RequestExecutionLevel user
Unicode True

!include "nsDialogs.nsh"
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "StrFunc.nsh"

${StrTrimNewLines}
${StrRep}

!define SOURCE_DIR ".."
!define REG_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Quake2RTX"
!define GAME_EXE "$INSTDIR\q2rtx.exe"
!define UNINSTALL_EXE "$INSTDIR\Uninstall.exe"

!define MUI_ICON "..\src\windows\res\q2rtx.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "WelcomeImage.bmp"
!define MUI_ABORTWARNING

Outfile "Quake2RTX-Setup.exe"
Name "Quake II RTX"

; InstallDir "$PROGRAMFILES\Quake2RTX"
InstallDir "C:\Games\Quake2RTX"
 
!insertmacro MUI_PAGE_WELCOME

!define MUI_DIRECTORYPAGE_TEXT_TOP "Choose the folder where to install Quake II RTX.$\r$\n\
Installing into a system folder like '$PROGRAMFILES' is NOT recommended.$\r$\n\
Installing into a folder with an existing installation of Quake II is also NOT recommended."

!insertmacro MUI_PAGE_DIRECTORY

!define MUI_COMPONENTSPAGE_SMALLDESC
!insertmacro MUI_PAGE_COMPONENTS

Var FullGameDir
!define MUI_PAGE_CUSTOMFUNCTION_PRE FullGamePage_Pre
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE FullGamePage_Leave
!define MUI_PAGE_HEADER_TEXT "Quake II Game Files"
!define MUI_PAGE_HEADER_SUBTEXT ""
!define MUI_DIRECTORYPAGE_TEXT_TOP "Choose the folder where the Quake II game files are located. The installer will copy the necessary files to the Quake II RTX install location."
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "Folder with the Quake II executable"
!define MUI_DIRECTORYPAGE_VARIABLE $FullGameDir
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_INSTFILES
;!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

 
Section "Engine Files (Required)" Section_Game
 
  	SectionIn RO

	SetOutPath "$INSTDIR"
	File "${SOURCE_DIR}\q2rtx.exe"
	File "${SOURCE_DIR}\q2rtxded.exe"
	File "${SOURCE_DIR}\q2rtxded-x86.exe"
	File "${SOURCE_DIR}\license.txt"
	File "${SOURCE_DIR}\notice.txt"
	File "${SOURCE_DIR}\readme.md"
	File "${SOURCE_DIR}\changelog.md"

	SetOutPath "$INSTDIR\baseq2"
	File "${SOURCE_DIR}\baseq2\gamex86_64.dll"
	File "${SOURCE_DIR}\baseq2\gamex86.dll"

	SetCompress OFF
	File "${SOURCE_DIR}\baseq2\shaders.pkz"
	File "${SOURCE_DIR}\baseq2\blue_noise.pkz"
	File "${SOURCE_DIR}\baseq2\q2rtx_media.pkz"

	WriteUninstaller ${UNINSTALL_EXE}

	WriteRegStr HKLM "${REG_KEY}" "DisplayName" "Quake II RTX"
	WriteRegStr HKLM "${REG_KEY}" "DisplayIcon" "${GAME_EXE}"
	WriteRegStr HKLM "${REG_KEY}" "Publisher" "NVIDIA Corporation"
	WriteRegStr HKLM "${REG_KEY}" "UninstallString" "$\"${UNINSTALL_EXE}$\""

	${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
 	IntFmt $0 "0x%08X" $0
 	WriteRegDWORD HKLM "${REG_KEY}" "EstimatedSize" "$0"

SectionEnd

Section "Quake II Shareware Demo" Section_Shareware

	SetOutPath "$INSTDIR\baseq2"
	SetCompress AUTO
	File "${SOURCE_DIR}\baseq2\shareware\pak0.pak"
	
	SetOutPath "$INSTDIR\baseq2\players"
	File /r "${SOURCE_DIR}\baseq2\shareware\players\*"

SectionEnd

Section /o "Quake II Full Game" Section_FullGame

	CopyFiles "$FullGameDir\baseq2\pak*.pak" "$INSTDIR\baseq2"
	CopyFiles "$FullGameDir\baseq2\players" "$INSTDIR\baseq2"
	CopyFiles "$FullGameDir\baseq2\video" "$INSTDIR\baseq2"
	CopyFiles "$FullGameDir\music" "$INSTDIR\music"

SectionEnd

Section "Desktop Shortcut" Section_DesktopShortcut

	CreateShortCut "$DESKTOP\Quake II RTX.lnk" "${GAME_EXE}"

SectionEnd

;Section "Start Menu Shortcuts"
;
;	CreateDirectory "$SMPROGRAMS\Quake II RTX"
;	CreateShortCut "$SMPROGRAMS\Quake II RTX\Quake II RTX.lnk" "${GAME_EXE}"
;
;SectionEnd

Section "Uninstall"

	Delete "${UNINSTALL_EXE}"
	Delete "$DESKTOP\Quake II RTX.lnk"
	Delete "$SMPROGRAMS\Quake II RTX\Quake II RTX.lnk"
	DeleteRegKey HKLM "${REG_KEY}"
  	RMDir /r "$INSTDIR"

SectionEnd

!include "setup_gamefiles.nsh"

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${Section_Game} "Executable and media files for Quake II RTX"
  !insertmacro MUI_DESCRIPTION_TEXT ${Section_Shareware} "Install a copy of the Quake II Shareware Demo"
  !insertmacro MUI_DESCRIPTION_TEXT ${Section_FullGame} "Locate and copy the media files for the full game"
  !insertmacro MUI_DESCRIPTION_TEXT ${Section_DesktopShortcut} "Place a shortcut for the game onto the Desktop"
!insertmacro MUI_FUNCTION_DESCRIPTION_END