# Build ior at the commit CI pins (IOR_REF in .github/workflows/ci.yml) into a prefix laid out as
# php-src's win32/build/config.w32 expects (include\ior\ior.h, lib\ior.lib), with the flags of the core's
# build-ior-windows action, against the C runtime of the core build: /MDd for Debug_TS, /MD for Release_TS.
#
#     build-ior.ps1 -Config Debug|Release -Prefix <dir> [-Work <dir>] [-Generator <cmake generator>]
#
# -Generator is needed when cmake does not know the newest Visual Studio installed.
param(
    [Parameter(Mandatory)] [ValidateSet('Debug', 'Release')] [string] $Config,
    [Parameter(Mandatory)] [string] $Prefix,
    [string] $Work = (Join-Path $env:TEMP 'ior-src'),
    [string] $Generator
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

$ci = Get-Content (Join-Path $PSScriptRoot '..\..\.github\workflows\ci.yml') -Raw
$ref = [regex]::Match($ci, '(?m)^\s*IOR_REF:\s*([0-9a-f]{40})').Groups[1].Value
if (-not $ref) {
    throw 'no IOR_REF in .github/workflows/ci.yml'
}

$runtime = if ($Config -eq 'Debug') { 'MultiThreadedDebugDLL' } else { 'MultiThreadedDLL' }
$generatorArgs = if ($Generator) { @('-G', $Generator) } else { @() }
$build = Join-Path $Work "build-$Config"

git init -q $Work
git -C $Work fetch -q --depth 1 https://github.com/libior/ior.git $ref
git -C $Work checkout -q --force FETCH_HEAD
cmake -S $Work -B $build @generatorArgs -A x64 "-DCMAKE_MSVC_RUNTIME_LIBRARY=$runtime" `
    -DIOR_BUILD_TESTS=OFF -DIOR_BUILD_BENCH=OFF
cmake --build $build --config $Config --parallel
cmake --install $build --config $Config --prefix $Prefix

if (-not (Test-Path (Join-Path $Prefix 'include\ior\ior.h')) -or -not (Test-Path (Join-Path $Prefix 'lib\ior.lib'))) {
    throw "ior install prefix $Prefix is incomplete"
}
