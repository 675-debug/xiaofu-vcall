@echo off
setlocal EnableExtensions
rem ============================================================
rem  xiaofu-vcall-client Windows x64 Release deploy (Qt 6.11.1 / MSVC2022)
rem  Usage:
rem    deploy_windows_qt6.bat                build + deploy + zip
rem    deploy_windows_qt6.bat --deploy-only  deploy existing exe only
rem ============================================================

set "ROOT=%~dp0"
set "QT_BIN=D:\app\Qt6.11\6.11.1\msvc2022_64\bin"
set "VS_VARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "SDK_VER=10.0.19041.0"
set "BUILD_DIR=%ROOT%build-xiaofu-vcall-client-Qt_6_11_1_MSVC2022_64-Release"
set "RELEASE_OUT=%BUILD_DIR%\release_out"
set "APP_EXE=%RELEASE_OUT%\xiaofu-vcall-client.exe"
set "DIST_ROOT=%ROOT%dist"
set "DIST_NAME=xiaofu-vcall-client-win64"
set "DIST_DIR=%DIST_ROOT%\%DIST_NAME%"
set "ZIP_PATH=%DIST_ROOT%\%DIST_NAME%.zip"

echo [deploy] ROOT=%ROOT%
echo [deploy] QT_BIN=%QT_BIN%

if /i "%~1"=="--deploy-only" goto deploy

echo [deploy] Building Release ...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
call "%VS_VARS%" x64 %SDK_VER%
if errorlevel 1 goto :fail
pushd "%BUILD_DIR%"
"%QT_BIN%\qmake.exe" -o Makefile "..\client\xiaofu-vcall-client.pro" -spec win32-msvc CONFIG+=release -after "DESTDIR=%RELEASE_OUT%"
if errorlevel 1 ( popd & goto :fail )
nmake
if errorlevel 1 ( popd & goto :fail )
popd

:deploy
if not exist "%APP_EXE%" (
    echo [deploy] ERROR Release exe not found: "%APP_EXE%"
    echo [deploy] Run deploy_windows_qt6.bat without --deploy-only to build it first.
    goto :fail
)
echo [deploy] Using Release exe: %APP_EXE%

rem ---- safety: only clean the fixed dist subdirectory
if /i not "%DIST_DIR%"=="%DIST_ROOT%\%DIST_NAME%" (
    echo [deploy] ERROR refusing to clean unexpected path: "%DIST_DIR%"
    goto :fail
)
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"
if errorlevel 1 goto :fail

copy /y "%APP_EXE%" "%DIST_DIR%\" >nul
if errorlevel 1 goto :fail
copy /y "%ROOT%client\asr.url" "%DIST_DIR%\asr.url" >nul
if errorlevel 1 goto :fail

echo [deploy] Running windeployqt --release --compiler-runtime ...
call "%VS_VARS%" x64 %SDK_VER% >nul 2>&1
if errorlevel 1 goto :fail
set "PATH=%QT_BIN%;%PATH%"
"%QT_BIN%\windeployqt.exe" --release --compiler-runtime "%DIST_DIR%\xiaofu-vcall-client.exe"
if errorlevel 1 goto :fail
echo [deploy] windeployqt OK

rem ---- verify key Qt6/WebEngine files
set "MISSING=0"
call :check "%DIST_DIR%\xiaofu-vcall-client.exe"
call :check "%DIST_DIR%\Qt6Core.dll"
call :check "%DIST_DIR%\Qt6Gui.dll"
call :check "%DIST_DIR%\Qt6Widgets.dll"
call :check "%DIST_DIR%\Qt6Network.dll"
call :check "%DIST_DIR%\Qt6WebChannel.dll"
call :check "%DIST_DIR%\Qt6WebEngineCore.dll"
call :check "%DIST_DIR%\Qt6WebEngineWidgets.dll"
call :check "%DIST_DIR%\asr.url"
if exist "%DIST_DIR%\Qt6Multimedia.dll" (
    echo [deploy] OK: %DIST_DIR%\Qt6Multimedia.dll
) else (
    echo [deploy] INFO: Qt6Multimedia.dll not linked in Qt6 build - expected
)
call :check "%DIST_DIR%\QtWebEngineProcess.exe"
call :check "%DIST_DIR%\platforms\qwindows.dll"
call :check "%DIST_DIR%\resources\qtwebengine_resources.pak"
call :check "%DIST_DIR%\resources\qtwebengine_resources_100p.pak"
call :check "%DIST_DIR%\resources\qtwebengine_resources_200p.pak"
call :check "%DIST_DIR%\resources\icudtl.dat"
if "%MISSING%"=="1" (
    echo [deploy] ERROR some required files are missing.
    goto :fail
)
echo [deploy] QtWebEngine key files: OK

rem ---- slim: drop devtools resources (not needed at runtime)
if exist "%DIST_DIR%\resources\qtwebengine_devtools_resources.pak" (
    del /q "%DIST_DIR%\resources\qtwebengine_devtools_resources.pak"
    echo [deploy] removed resources\qtwebengine_devtools_resources.pak
)

rem ---- slim: keep only zh-CN / en-US WebEngine locales
set "LOCALES_DIR=%DIST_DIR%\translations\qtwebengine_locales"
if exist "%LOCALES_DIR%" (
    for %%F in ("%LOCALES_DIR%\*.pak") do (
        if /i not "%%~nxF"=="zh-CN.pak" (
            if /i not "%%~nxF"=="en-US.pak" (
                del /q "%%F"
            )
        )
    )
    echo [deploy] WebEngine locales slimmed to zh-CN + en-US
)

rem ---- create zip
echo [deploy] Creating ZIP ...
if exist "%ZIP_PATH%" del /q "%ZIP_PATH%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%DIST_DIR%' -DestinationPath '%ZIP_PATH%' -CompressionLevel Optimal"
if errorlevel 1 goto :fail
echo [deploy] ZIP created: %ZIP_PATH%
echo [deploy] DONE.
exit /b 0

:check
if not exist "%~1" (
    echo [deploy] MISSING: %~1
    set "MISSING=1"
) else (
    echo [deploy] OK: %~1
)
goto :eof

:fail
echo [deploy] FAILED.
exit /b 1
