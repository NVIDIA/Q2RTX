@echo off
SET SEVENZIP="C:\Program Files\7-Zip\7z.exe"
SET DEST=q2rtx_media.pkz
PUSHD ..\baseq2
SET SOURCES=env maps models overrides pics sound sprites textures materials prefetch.txt pt_toggles.cfg q2rtx.cfg q2rtx.menu
IF EXIST %DEST% DEL %DEST%
%SEVENZIP% a -tzip %DEST% %SOURCES%
POPD

PUSHD ..\rogue
SET SOURCES_ROGUE=maps
IF EXIST %DEST% DEL %DEST%
%SEVENZIP% a -tzip %DEST% %SOURCES_ROGUE%
POPD
