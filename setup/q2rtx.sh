#!/bin/bash

BIN_PREFIX="."

# If the game is installed via a package manager q2rtx won't be in the same
# directory as q2rtx.sh
if [[ -d "/usr/share/quake2rtx" ]]; then
	BIN_PREFIX="/usr/share/quake2rtx/bin"
fi

# Generate the user's game dir if doesn't exist
if [[ ! -d "${HOME}/.quake2rtx/baseq2" ]]; then
	mkdir -p "${HOME}/.quake2rtx/baseq2"
fi

# Only run this script on first-launch
if [[ ! -f "${HOME}/.quake2rtx/.retail_checked" ]]; then
	${BIN_PREFIX}/find-retail-paks.sh
	touch ${HOME}/.quake2rtx/.retail_checked
fi

${BIN_PREFIX}/q2rtx "$@"
