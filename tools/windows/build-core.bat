@echo off
rem Build the core checkout with this repository as ext\true_async (a junction) and copy the
rem dependencies' DLLs next to php.exe. Release_TS is configured as the CI windows job's build
rem (.github/workflows/ci.yml), Debug_TS with the extensions the lists load; both add test_scheduler
rem for the core's suites. Runs in the php-sdk shell:
rem
rem     phpsdk-vs18-x64.bat -t tools\windows\build-core.bat
rem
rem with these variables set: CONFIG (Debug or Release), PHP_SRC (the core checkout at CORE_REF),
rem IOR_PREFIX (build-ior.ps1's prefix of the same CONFIG), DEPS_DIR, OBJ_DIR, and optionally ADD_CONF
rem (more configure switches, as in CI). php.exe lands in %OBJ_DIR%\Debug_TS or %OBJ_DIR%\Release_TS.
setlocal

for %%v in (CONFIG PHP_SRC IOR_PREFIX DEPS_DIR OBJ_DIR) do (
	if not defined %%v (
		echo %%v is not set
		exit /b 1
	)
)

if /i "%CONFIG%" equ "Debug" (
	set CONFIG_ARGS=--enable-debug --enable-sockets --with-openssl --with-curl --with-mysqli --enable-pdo --with-pdo-mysql
	set BUILD_DIR=%OBJ_DIR%\Debug_TS
) else if /i "%CONFIG%" equ "Release" (
	set CONFIG_ARGS=--enable-snapshot-build --disable-debug-pack --enable-native-intrinsics=AVX2
	set BUILD_DIR=%OBJ_DIR%\Release_TS
) else (
	echo CONFIG is Debug or Release, not %CONFIG%
	exit /b 1
)

if not exist "%PHP_SRC%\ext\true_async" (
	mklink /J "%PHP_SRC%\ext\true_async" "%~dp0..\.." || exit /b 2
)

cd /d "%PHP_SRC%"
cmd /c phpsdk_deps --update --no-backup --branch master --stability staging --deps "%DEPS_DIR%" --crt vs18
if %errorlevel% neq 0 exit /b 3

cmd /c buildconf.bat --force
if %errorlevel% neq 0 exit /b 4

rem The warnings php-src's build_task.bat lets through /WX.
set CFLAGS=/W3 /WX /wd4018 /wd4146 /wd4244 /wd4267

cmd /c configure.bat %CONFIG_ARGS% ^
	--without-analyzer ^
	--enable-object-out-dir=%OBJ_DIR% ^
	--with-php-build=%DEPS_DIR% ^
	--with-ior=%IOR_PREFIX% ^
	--enable-test-scheduler ^
	--enable-true-async=shared ^
	--enable-true-async-test-hooks=yes ^
	%ADD_CONF% ^
	--disable-test-ini
if %errorlevel% neq 0 exit /b 5

rem configure writes the OS caption into PHP_BUILD_SYSTEM in the ANSI code page; on a localized Windows (a
rem Russian one names itself in CP1251) /utf-8 /WX then stops every file on C4828. Only that line loses its
rem non-ASCII bytes. sed is php-sdk's, on PATH in its shell; in msys' default UTF-8 locale [^ -~] does not
rem match bytes invalid in UTF-8, hence LC_ALL=C for sed alone.
set LC_ALL=C
sed -i -e "/^#define PHP_BUILD_SYSTEM /{s/[^ -~]//g;s/\" */\"/}" main\config.w32.h
if %errorlevel% neq 0 exit /b 8
set LC_ALL=

rem An object depends on its .c file only (win32\build\confutils.js), so after a change to a header
rem nmake would link this repository's objects built against the old one.
rem Only the objects go: configure made the directories, and nmake does not make them again.
if exist "%BUILD_DIR%\ext\true_async" del /s /q "%BUILD_DIR%\ext\true_async\*.obj" > nul
nmake /NOLOGO
if %errorlevel% neq 0 exit /b 6

copy /y "%DEPS_DIR%\bin\*.dll" "%BUILD_DIR%\" > nul || exit /b 7
exit /b 0
