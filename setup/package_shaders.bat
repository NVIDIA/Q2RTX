@echo off
PUSHD ..\baseq2
SET SEVENZIP="C:\Program Files\7-Zip\7z.exe"
SET SOURCES=shader_vkpt
SET DEST=shaders.pkz
IF EXIST %DEST% DEL %DEST%
%SEVENZIP% a -tzip %DEST% %SOURCES%
POPD