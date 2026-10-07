# Windows build and tests

These scripts build the pinned core (`CORE_REF`, `IOR_REF` in `.github/workflows/ci.yml`) with this
repository as `ext\true_async` on a Windows x64 machine and run the `pocs-win` lane and the core's
own suites on it. The CI windows job does the same for Release_TS with php-src's CI scripts;
these scripts add Debug_TS, which CI does not build.

## Prerequisites

- Visual Studio 2026 Build Tools with the C++ workload (MSVC 14.50 or later; the core refuses an
  older compiler), for example
  `winget install --id Microsoft.VisualStudio.BuildTools --exact --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"`.
- php-sdk-binary-tools 2.8.4:
  `git clone --branch php-sdk-2.8.4 --depth 1 https://github.com/php/php-sdk-binary-tools.git <sdk>`.
- cmake, git, PowerShell 7.4 or later (`build-ior.ps1` stops on a failed native command only from
  7.4 on), Python 3.
- The core: a checkout of true-async/php-src at `CORE_REF`.

## Steps

```bat
pwsh tools\windows\build-ior.ps1 -Config Debug -Prefix E:\php\ior-dbg

set CONFIG=Debug
set PHP_SRC=E:\php\php-src
set IOR_PREFIX=E:\php\ior-dbg
set DEPS_DIR=E:\php\deps-vs18
set OBJ_DIR=E:\php\obj
<sdk>\phpsdk-vs18-x64.bat -t tools\windows\build-core.bat

tools\windows\run-suites.bat E:\php\php-src E:\php\obj\Debug_TS E:\php\results\debug
```

The same with `Release` and a Release prefix of ior builds Release_TS as CI does
(`--enable-snapshot-build`, which also builds every other extension directory in the checkout;
`set ADD_CONF=--disable-<name>` leaves one out).

`build-core.bat` links the repository into `ext\true_async` as a junction and compiles the
extension anew on each run. The Makefile makes an object depend on its `.c` file only
(`win32\build\confutils.js`), so `nmake` alone in `%PHP_SRC%` rebuilds after a change to a `.c`
file but links stale objects after a change to a header: delete the `.obj` files under
`%OBJ_DIR%\<CONFIG>_TS\ext\true_async` first (not the directories, which nmake does not make
again). For the same reason a new `CORE_REF` needs an empty `OBJ_DIR`. An `ext\true_async` that
already exists (a copy, or a junction to another checkout) is built as it is.

## Pitfalls

- The php-sdk shell needs the environment variable `NoDefaultCurrentDirectoryInExePath` unset:
  `cmd /c "set NoDefaultCurrentDirectoryInExePath=&& <sdk>\phpsdk-vs18-x64.bat -t ..."`.
- `-s <toolset>` of php-sdk asks vswhere without `-products`, which leaves Build Tools out and may
  pick an older Community install; leave it out.
- cmake picks its newest known Visual Studio generator; one that does not know Visual Studio 2026
  needs `-Generator "Visual Studio 17 2022"` for ior, which still links with the 2026 build of PHP.

## Comparing with Linux

`run-suites.bat` writes the output of `ext\test_scheduler\tests`, `ext\standard\tests\poll` and
`ext\standard\tests\streams\hooks` to `suites.out`. On Linux, the same suites of the debug core:

```sh
TEST_PHP_EXECUTABLE=sapi/cli/php sapi/cli/php run-tests.php -j8 -q \
    ext/test_scheduler/tests ext/standard/tests/poll ext/standard/tests/streams/hooks > linux.out
tools/results.py --diff linux.out suites.out
```

The result of that comparison on `CORE_REF` is in `dev/PLAN.md`, step S1.5.
