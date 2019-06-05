@echo off

IF "%BUILD_DIRECTORY%" == "" SET BUILD_DIRECTORY=build

IF NOT EXIST %BUILD_DIRECTORY% (
	echo build directory not found
	exit /b 1
)

cd %BUILD_DIRECTORY%

IF EXIST *.sln (
    set "EXTRA_FLAGS=-- /nologo /verbosity:minimal"
)

cmake --build . --target shaders %EXTRA_FLAGS%
cd ..

