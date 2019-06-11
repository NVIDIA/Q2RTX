
Function FullGamePage_Pre
    ${IfNot} ${SectionIsSelected} ${Section_FullGame}
    	Abort
	${EndIf}

	Var /GLOBAL GogDir
	Var /GLOBAL BethesdaDir
	Var /GLOBAL SteamDir
	Var /GLOBAL CurLine
	Var /GLOBAL FileHandle
	Var /GLOBAL CharIndex

	; try to find Quake 2 in the GOG library - this is the most complete version
	ReadRegStr $GogDir HKLM "SOFTWARE\WOW6432Node\GOG.com\Games\1441704824" "PATH"
	${IfNot} ${Errors}
		StrCpy $FullGameDir "$GogDir"
		IfFileExists "$FullGameDir\quake2.exe" SearchDone
		
		; not found - clear the variable
		StrCpy $FullGameDir ""
	${EndIf}

	; try to find Quake 2 in the Bethesda library
	ReadRegStr $BethesdaDir HKLM "SOFTWARE\Bethesda Softworks\Bethesda.net" "installLocation"
	${IfNot} ${Errors}
		StrCpy $FullGameDir "$BethesdaDir\games\Quake II"
		IfFileExists "$FullGameDir\quake2.exe" SearchDone

		; not found - clear the variable
		StrCpy $FullGameDir ""
	${EndIf}

	; try to find Quake 2 in the Steam folders
	ReadRegStr $SteamDir HKLM "SOFTWARE\Valve\Steam" "InstallPath"
	${IfNot} ${Errors}
		; first, look in the default Steam library
		StrCpy $FullGameDir "$SteamDir\steamapps\common\Quake 2"
		IfFileExists "$FullGameDir\quake2.exe" SearchDone

		; not found - clear the variable
		StrCpy $FullGameDir ""

		; now look for other libraries which are listed in <libraryfolders.vdf>
		FileOpen $FileHandle "$SteamDir\steamapps\libraryfolders.vdf" r
		IfErrors SearchDone

		NextLine:
			FileRead $FileHandle $CurLine
			StrCmp $CurLine '' FileDone
			
 			${StrTrimNewLines} $CurLine $CurLine

			; test if the line ends with a quote
			StrCpy $0 $CurLine 1 -1
			StrCmp $0 '"' 0 NextLine

			StrCpy $CharIndex -2
			NextChar:
				StrCpy $0 $CurLine 1 $CharIndex
				IntOp $CharIndex $CharIndex - 1

				StrCmp $0 '' NextLine 0 ; beginning of line, no quote - go to next line
				StrCmp $0 '"' 0 NextChar ; if this is not a quote, try next character; otherwise, go on

				IntOp $CharIndex $CharIndex + 2
				IntOp $1 -1 - $CharIndex
				StrCpy $FullGameDir $CurLine $1 $CharIndex ; extract the quoted substring from CurLine
				StrCpy $FullGameDir "$FullGameDir\steamapps\common\Quake 2"
				${StrRep} $FullGameDir $FullGameDir "\\" "\"

				IfFileExists "$FullGameDir\quake2.exe" FileDone	

				; not a valid path - clear FullGameDir and keep looking
				StrCpy $FullGameDir ""
				Goto NextLine

		FileDone:
		FileClose $FileHandle

		SearchDone:
	${EndIf}
FunctionEnd

Function FullGamePage_Leave
	IfFileExists "$FullGameDir\baseq2\pak0.pak" exists 0
		MessageBox MB_OK "Game files (baseq2\pak*.pak) are not found in the specified location. Please specify the correct location."
		Abort
	exists:
FunctionEnd

Function .onInit
StrCpy $1 ${Section_Shareware}
FunctionEnd

Function .onSelChange
!insertmacro StartRadioButtons $1
!insertmacro RadioButton "${Section_Shareware}"
!insertmacro RadioButton "${Section_FullGame}"
!insertmacro EndRadioButtons
FunctionEnd
