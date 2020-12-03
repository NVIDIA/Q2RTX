
RequestExecutionLevel user
Unicode True

!include "nsDialogs.nsh"
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "StrFunc.nsh"

${StrTrimNewLines}
${StrRep}

!define SOURCE_DIR ".."

!define MUI_ICON "..\src\windows\res\q2rtx.ico"
!define MUI_ABORTWARNING

Outfile "Quake2RTX-Steam-Setup.exe"
Name "Quake II RTX"

InstallDir "$EXEDIR\.."
 
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
!insertmacro MUI_LANGUAGE "English"

 
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

!include "setup_gamefiles.nsh"

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${Section_Shareware} "Install a copy of the Quake II Shareware Demo"
  !insertmacro MUI_DESCRIPTION_TEXT ${Section_FullGame} "Locate and copy the media files for the full game"
!insertmacro MUI_FUNCTION_DESCRIPTION_END