@echo off
PUSHD ..
SET SEVENZIP="C:\Program Files\7-Zip\7z.exe"
SET SOURCES=q2rtx.exe q2rtxded.exe baseq2\gamex86_64.dll baseq2\shaders.pkz baseq2\blue_noise.pkz baseq2\q2rtx_media.pkz setup\Quake2RTX-Steam-Setup.exe
SET DEST=setup\q2rtx_steam.zip
IF EXIST %DEST% DEL %DEST%
%SEVENZIP% a -mx1 -tzip %DEST% %SOURCES%
POPD