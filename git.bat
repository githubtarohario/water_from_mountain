@echo off
rem =============================================================================
rem git.bat
rem   このプロジェクトを GitHub (https://github.com/githubtarohario/water_from_mountain)
rem   に更新するためのコマンド集。コマンドプロンプトからそのまま実行できる。
rem
rem   使い方:
rem     git.bat                    現在の状態を表示 (変更点・ブランチ・最近の履歴)
rem     git.bat save "メッセージ"   変更をすべて記録して GitHub へ送る (add + commit + push)
rem     git.bat push               記録済みの変更を GitHub へ送るだけ
rem     git.bat pull               GitHub の最新を取り込む
rem     git.bat main               いまのブランチの内容を main に反映して GitHub へ送る
rem     git.bat branch 名前         新しいブランチを作って GitHub にも登録する
rem     git.bat switch 名前         別のブランチに切り替える
rem     git.bat log                履歴を 20 件表示
rem     git.bat diff               直前の記録との違いを表示
rem     git.bat undo               まだ記録していない変更を取り消す (確認あり)
rem     git.bat open               GitHub のページをブラウザで開く
rem =============================================================================
setlocal
cd /d "%~dp0"

rem ---- リポジトリかどうか確認 ----
git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
    echo [エラー] ここは Git リポジトリではありません: %cd%
    exit /b 1
)

rem ---- 現在のブランチ名を取得 ----
for /f "delims=" %%b in ('git rev-parse --abbrev-ref HEAD') do set BRANCH=%%b

if "%~1"==""        goto :status
if /i "%~1"=="status" goto :status
if /i "%~1"=="save"   goto :save
if /i "%~1"=="push"   goto :push
if /i "%~1"=="pull"   goto :pull
if /i "%~1"=="main"   goto :tomain
if /i "%~1"=="branch" goto :branch
if /i "%~1"=="switch" goto :switch
if /i "%~1"=="log"    goto :log
if /i "%~1"=="diff"   goto :diff
if /i "%~1"=="undo"   goto :undo
if /i "%~1"=="open"   goto :open
echo [エラー] 知らないコマンドです: %~1
echo.
goto :usage

rem -----------------------------------------------------------------------------
:status
echo ============================================================
echo  ブランチ: %BRANCH%
echo ============================================================
echo.
echo ---- 変更されたファイル ----
git status --short
if errorlevel 1 exit /b 1
echo.
echo ---- GitHub との差 ----
git status -sb -uno
echo.
echo ---- 最近の履歴 ----
git --no-pager log --oneline -5
echo.
echo 変更を送るには:  git.bat save "何をしたかの説明"
goto :eof

rem -----------------------------------------------------------------------------
:save
if "%~2"=="" (
    echo [エラー] 記録するメッセージを指定してください。
    echo   例: git.bat save "粒子の色を変更"
    exit /b 1
)
echo ---- 変更を記録します ----
git add -A
if errorlevel 1 exit /b 1
rem 変更が無いときは commit が失敗するので、先に確認する
git diff --cached --quiet
if not errorlevel 1 (
    echo 記録する変更はありません。
    goto :push
)
git commit -m "%~2"
if errorlevel 1 exit /b 1
goto :push

rem -----------------------------------------------------------------------------
:push
echo.
echo ---- GitHub へ送信します (%BRANCH%) ----
git push -u origin %BRANCH%
if errorlevel 1 (
    echo.
    echo [エラー] 送信に失敗しました。
    echo   ・ネットワークを確認してください
    echo   ・他の場所で更新されている場合は  git.bat pull  を先に実行してください
    exit /b 1
)
echo.
echo 完了しました: https://github.com/githubtarohario/water_from_mountain/tree/%BRANCH%
goto :eof

rem -----------------------------------------------------------------------------
:pull
echo ---- GitHub の最新を取り込みます (%BRANCH%) ----
git pull --rebase origin %BRANCH%
if errorlevel 1 (
    echo [エラー] 取り込みに失敗しました。内容が衝突している可能性があります。
    exit /b 1
)
echo 完了しました。
goto :eof

rem -----------------------------------------------------------------------------
:tomain
if /i "%BRANCH%"=="main" (
    echo すでに main にいます。 git.bat push を使ってください。
    goto :eof
)
echo ---- %BRANCH% の内容を main に反映します ----
rem 作業ツリーを切り替えずに、いまのブランチの内容をそのまま main として送る
git push origin refs/heads/%BRANCH%:refs/heads/main
if errorlevel 1 (
    echo [エラー] main への反映に失敗しました。
    echo   main 側に別の変更があると拒否されます。その場合は git.bat pull を試してください。
    exit /b 1
)
rem 手元の main の印も同じ位置に進めておく (作業中のファイルはそのまま)
git branch -f main %BRANCH%
git fetch -q origin
echo.
echo 完了しました: https://github.com/githubtarohario/water_from_mountain/tree/main
goto :eof

rem -----------------------------------------------------------------------------
:branch
if "%~2"=="" (
    echo [エラー] 新しいブランチ名を指定してください。  例: git.bat branch dev2
    exit /b 1
)
git checkout -b "%~2"
if errorlevel 1 exit /b 1
git push -u origin "%~2"
if errorlevel 1 exit /b 1
echo 完了しました: ブランチ %~2 を作成しました。
goto :eof

rem -----------------------------------------------------------------------------
:switch
if "%~2"=="" (
    echo [エラー] 切り替えるブランチ名を指定してください。  例: git.bat switch main
    echo.
    echo ---- 使えるブランチ ----
    git --no-pager branch -a
    exit /b 1
)
git checkout "%~2"
if errorlevel 1 exit /b 1
echo 現在のブランチ: %~2
goto :eof

rem -----------------------------------------------------------------------------
:log
git --no-pager log --oneline --graph --decorate -20
goto :eof

rem -----------------------------------------------------------------------------
:diff
git --no-pager diff --stat
echo.
echo 詳しく見るには:  git diff
goto :eof

rem -----------------------------------------------------------------------------
:undo
echo [確認] まだ記録していない変更をすべて取り消します。元に戻せません。
set /p ANS="本当に実行しますか? (y/N): "
if /i not "%ANS%"=="y" (
    echo 中止しました。
    goto :eof
)
git checkout -- .
git clean -fd
echo 取り消しました。
goto :eof

rem -----------------------------------------------------------------------------
:open
start "" "https://github.com/githubtarohario/water_from_mountain"
goto :eof

rem -----------------------------------------------------------------------------
:usage
echo 使い方:
echo   git.bat                    状態を表示
echo   git.bat save "メッセージ"   変更を記録して GitHub へ送る
echo   git.bat push               記録済みの変更を送る
echo   git.bat pull               GitHub の最新を取り込む
echo   git.bat main               いまのブランチを main に反映する
echo   git.bat branch 名前         新しいブランチを作る
echo   git.bat switch 名前         ブランチを切り替える
echo   git.bat log / diff / undo / open
exit /b 1
