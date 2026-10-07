@echo off
rem Run the core's own suites of the two PoCs (test_scheduler, Io\Poll, the IO hooks) without the
rem extension, then this repository's pocs-win lane, on one build of build-core.bat. Python 3 on PATH.
rem
rem     run-suites.bat <php-src> <build dir with php.exe> <results dir>
rem
rem The suites' output goes to <results dir>\suites.out; tools\results.py --diff compares it per test
rem with a Linux run of the same suites (README.md).
setlocal

if "%~3" equ "" (
	echo usage: run-suites.bat ^<php-src^> ^<build dir with php.exe^> ^<results dir^>
	exit /b 1
)

set PHP_SRC=%~f1
set BUILD_DIR=%~f2
set RESULTS=%~f3
if not exist "%RESULTS%" mkdir "%RESULTS%"

cd /d "%PHP_SRC%"
"%BUILD_DIR%\php.exe" run-tests.php -p "%BUILD_DIR%\php.exe" -j4 -q --show-diff ^
	ext\test_scheduler\tests ext\standard\tests\poll ext\standard\tests\streams\hooks > "%RESULTS%\suites.out" 2>&1
set SUITES=%errorlevel%
findstr /b /c:"Tests failed" /c:"Tests passed" /c:"Tests skipped" "%RESULTS%\suites.out"

cd /d "%~dp0..\.."
set TRUE_ASYNC_WIN_BUILD=%BUILD_DIR%
set TRUE_ASYNC_CORE_SRC=%PHP_SRC%
python tools\test.py --lane pocs-win
if %errorlevel% neq 0 exit /b %errorlevel%
exit /b %SUITES%
