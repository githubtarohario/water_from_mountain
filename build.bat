@echo off
rem =============================================================================
rem build.bat
rem   Visual Studio を開かずにコマンドラインでビルドするスクリプト。
rem   vswhere で Visual Studio の場所を探し、vcvars64.bat で環境を整えてから
rem   cl.exe で src\*.cpp をまとめてコンパイルする。
rem   出力: bin\MountainParticles.exe
rem =============================================================================
setlocal
cd /d "%~dp0"

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    echo vswhere.exe が見つかりません。Visual Studio 2019/2022 をインストールしてください。
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" (
    echo C++ ツールセットを持つ Visual Studio が見つかりません。
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

if not exist bin mkdir bin
if not exist obj mkdir obj

rem /utf-8   : ソースを UTF-8 として扱う (日本語コメント対策)
rem /openmp  : SPH の並列化 (#pragma omp) を有効化
rem /O2      : 速度最適化
cl /nologo /std:c++17 /EHsc /O2 /W3 /utf-8 /openmp /DUNICODE /D_UNICODE /DNOMINMAX /DNDEBUG ^
   /Fo"obj\\" /Fe"bin\MountainParticles.exe" src\*.cpp ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 (
    echo ビルドに失敗しました。
    exit /b 1
)

echo.
echo ビルド成功: bin\MountainParticles.exe
echo 実行するには run.bat を使うか、bin\MountainParticles.exe を起動してください。
endlocal
