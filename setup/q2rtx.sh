#!/bin/bash

BIN_PREFIX="."

mkdir -p "${XDG_DATA_HOME:="${HOME}/.local/share"}"

# If the game is installed via a package manager q2rtx won't be in the same
# directory as q2rtx.sh
if [[ -d "/usr/share/quake2rtx" ]]; then
	BIN_PREFIX="/usr/share/quake2rtx/bin"
fi

if [[ -d "${HOME}/.quake2rtx"  && ! -d "${XDG_DATA_HOME}/quake2rtx" ]]; then
        mv "${HOME}/.quake2rtx" "${XDG_DATA_HOME}/quake2rtx"
fi


# Generate the user's game dir if doesn't exist
if [[ ! -d "${XDG_DATA_HOME}/quake2rtx/baseq2" ]]; then
	mkdir -p "${XDG_DATA_HOME}/quake2rtx/baseq2"
fi

# Only run this script on first-launch
if [[ ! -f "${XDG_DATA_HOME}/quake2rtx/.retail_checked" ]]; then
	${BIN_PREFIX}/find-retail-paks.sh
	touch ${XDG_DATA_HOME}/quake2rtx/.retail_checked
fi

${BIN_PREFIX}/q2rtx "$@"
