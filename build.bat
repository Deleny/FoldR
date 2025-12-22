@echo off
echo ========================================
echo Building Folder Widget (C++ Win32)
echo ========================================

:: Check for CMake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake not found! Please install CMake.
    echo Download from: https://cmake.org/download/
    pause
    exit /b 1
)

:: Create build directory
if not exist build mkdir build
cd build

:: Configure with CMake
echo.
echo Configuring project...
cmake .. -G "Visual Studio 17 2022" -A x64

if %ERRORLEVEL% neq 0 (
    echo.
    echo Trying with Visual Studio 2019...
    cmake .. -G "Visual Studio 16 2019" -A x64
)

if %ERRORLEVEL% neq 0 (
    echo.
    echo Trying with MinGW...
    cmake .. -G "MinGW Makefiles"
)

if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

:: Build
echo.
echo Building...
cmake --build . --config Release

if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: Build failed!
    pause
    exit /b 1
)

:: Copy config file
copy /Y "..\config\folders.json" "Release\folders.json" >nul 2>&1
copy /Y "..\config\folders.json" ".\folders.json" >nul 2>&1

echo.
echo ========================================
echo Build successful!
echo Executable: build\Release\FolderWidget.exe
echo ========================================
echo.

pause
