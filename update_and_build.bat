@echo off
rem Re-pulls latest source from GitHub and rebuilds the application.
cd /d "%~dp0"
echo [1/3] Fetching updates...
git -c http.https://github.com/.extraheader="AUTHORIZATION: basic %GITHUB_AUTH%" fetch origin main
echo [2/3] Pulling latest main...
git -c http.https://github.com/.extraheader="AUTHORIZATION: basic %GITHUB_AUTH%" pull origin main
echo [3/3] Rebuilding (MSVC Release)...
cmake --build build --config Release
echo.
echo Update complete. Executable: build\Release\hand_gesture_control.exe
pause
