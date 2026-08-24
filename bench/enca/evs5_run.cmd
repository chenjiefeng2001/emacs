@echo off
rem EVS-5 backend attribution runner (streamed).
cd /d %~dp0..\..\test\enca
make enca_tests > build_evs5.log 2>&1
if errorlevel 1 (
  echo BUILD_FAIL
  type build_evs5.log | findstr /C:"error"
  exit /b 1
)
copy /y enca_tests evs5.exe >nul
del evs5_run.log 2>nul
echo BUILD_OK
evs5.exe > evs5_run.log 2>&1
echo EXITCODE=%ERRORLEVEL% >> evs5_run.log
echo DONE
