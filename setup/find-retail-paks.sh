#!/bin/bash

DEST_DIR="${HOME}/.quake2rtx"

# Check for zenity/yad
ZEN=""

# Default to not copying retail files (since we can't distribute them, and not
# every player will have the full game)
NO_COPY_RETAIL=1

copy_retail_files() {
	FULL_GAME_DIR="$1"
	pushd "${FULL_GAME_DIR}"
	mkdir -p "${DEST_DIR}/baseq2"
	cp baseq2/pak*.pak "${DEST_DIR}/baseq2"
	cp -R baseq2/players "${DEST_DIR}/baseq2"
	cp -R baseq2/music "${DEST_DIR}/baseq2"
	# GoG version of game puts music in basedir
	cp -R music "${DEST_DIR}/baseq2"
	popd
}


# which zenity
if [[ -f "/usr/bin/zenity" ]]; then
	ZEN="$(which zenity)"
	# XXX[ljm] WAR steam-runtime bug: https://github.com/ValveSoftware/steam-runtime/issues/104
	# The steam-runtime copy of zenity relies on a older zenity.ui file
	# version, and won't work on more modern distros
	ZEN="/usr/bin/zenity"
	ZEN_QUESTION="${ZEN} --question --text"
	ZEN_INFO="${ZEN} --info --text"
	ZEN_DIR_SELECT="${ZEN} --file-selection --directory"
else
	which kdialog
	if [[ ! -z "$?" ]]; then
		ZEN="$(which kdialog)"
		ZEN_QUESTION="${ZEN} --title Q2RTX --warningyesnocancel"
		ZEN_INFO="${ZEN} --title Q2RTX --msgbox"
		ZEN_DIR_SELECT="${ZEN} --title Q@RTX --getexistingdirectory"
	else
		ZEN=0
	fi
fi

if [[ ! -z ${ZEN} ]]; then
	${ZEN_QUESTION} "Do you own the Retail copy of Quake 2 and would like to import the PAK files to Quake 2 RTX?"
	# XXX[ljm] -z isn't working for some reason :(
	if [[ "0" -eq "$?" ]]; then
		NO_COPY_RETAIL=0
	fi
fi

# XXX[ljm] -z isn't working for some reason :(
if [[ "0" -eq ${NO_COPY_RETAIL} ]]; then

	# Check usual install location
	if [[ -d "${HOME}/.steam/steam/steamapps/common/Quake 2" ]]; then
		copy_retail_files "${HOME}/.steam/steam/steamapps/common/Quake 2"
		exit 0;
	fi

	# Prompt user for custom existing-install location
	if [[ ! -z ${ZEN} ]]; then
		${ZEN_INFO} "Please select your existing Quake 2 Retail Installation Directory."
		copy_retail_files "$(${ZEN_DIR_SELECT})"
		exit 0;
	fi
fi

# No retail found or user wasn't interested
exit 1
