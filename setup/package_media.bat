@echo off
PUSHD ..\baseq2
SET SEVENZIP="C:\Program Files\7-Zip\7z.exe"
SET SOURCES=env maps models overrides pics sound sprites textures materials prefetch.txt pt_toggles.cfg q2rtx.cfg q2rtx.menu sky_clusters.txt
SET DEST=q2rtx_media.pkz
IF EXIST %DEST% DEL %DEST%
%SEVENZIP% a -tzip %DEST% %SOURCES%
POPD