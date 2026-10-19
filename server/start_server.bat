@echo off
chcp 65001 >nul
rem ============================================================
rem  疲劳检测服务端 (mock 后端，无需模型/GPU)
rem  双击运行即可；关掉这个窗口 = 停止服务端。
rem ============================================================
cd /d "%~dp0"

if not exist "build\default\fatigue_server.exe" (
  echo [错误] 找不到 build\default\fatigue_server.exe
  echo 请先在 server 目录执行: cmake --build --preset default
  goto :end
)

echo 正在启动服务端 (UDP 9000)...
build\default\fatigue_server.exe --backend mock --port 9000

echo.
echo [提示] 服务端已退出，错误码 %errorlevel%
echo   若端口被占用：先关闭其它 fatigue_server.exe 进程，或改用其它端口。

:end
pause
