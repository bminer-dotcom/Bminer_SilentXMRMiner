# Builds static libtoxcore.a + libsodium.a for MinGW and vendors them into
# ThirdParty/toxcore/ so the client CMake (ENABLE_TOX_C2) can link them.
#
# Prereqs (all in PATH): cmake, gcc/g++ (mingw-w64), mingw32-make, git.
#
# Run:  powershell -ExecutionPolicy Bypass -File ThirdParty\build_toxcore.ps1
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$ClientRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ThirdParty = Join-Path $ClientRoot 'ThirdParty'
$WorkDir    = Join-Path $ThirdParty '.build'
$OutDir     = Join-Path $ThirdParty 'toxcore'

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

# ---------------------------------------------------------------------------
# 1. libsodium (prebuilt MSYS2 package, static). Adjust the version if needed.
# ---------------------------------------------------------------------------
$LibsodiumUrl = 'https://repo.msys2.org/mingw/mingw64/mingw-w64-x86_64-libsodium-1.0.22-3-any.pkg.tar.zst'
$LibsodiumPkg = Join-Path $WorkDir 'libsodium.pkg.tar.zst'
if (-not (Test-Path $LibsodiumPkg)) {
    Invoke-WebRequest -Uri $LibsodiumUrl -OutFile $LibsodiumPkg -UseBasicParsing
}
$SodiumDir = Join-Path $WorkDir 'sodium'
if (-not (Test-Path (Join-Path $SodiumDir 'mingw64\lib\libsodium.a'))) {
    New-Item -ItemType Directory -Force -Path $SodiumDir | Out-Null
    tar -xf $LibsodiumPkg -C $SodiumDir
}
$SodiumInc = (Resolve-Path (Join-Path $SodiumDir 'mingw64\include')).Path
$SodiumLib = (Resolve-Path (Join-Path $SodiumDir 'mingw64\lib\libsodium.a')).Path

# ---------------------------------------------------------------------------
# 2. Fake pkg-config (c-toxcore's CMake requires it). Answers only libsodium.
# ---------------------------------------------------------------------------
$FakeSrc = Join-Path $WorkDir 'fake_pkgconfig.c'
@"
#include <stdio.h>
#include <string.h>
static int is_sodium(const char *s) {
    if (strcmp(s,"libsodium")==0 || strcmp(s,"sodium")==0) return 1;
    if (strncmp(s,"libsodium ",10)==0 || strncmp(s,"sodium ",7)==0) return 1;
    if (strncmp(s,"libsodium>",10)==0 || strncmp(s,"sodium>",7)==0) return 1;
    return 0;
}
int main(int argc, char **argv) {
    const char *module = NULL;
    int exists=0, modver=0, cflags=0, libs=0;
    for (int i=1;i<argc;i++) {
        const char *a = argv[i];
        if (!strcmp(a,"--version")) { printf("0.29.2\n"); return 0; }
        else if (!strcmp(a,"--exists")) exists=1;
        else if (!strcmp(a,"--modversion")) modver=1;
        else if (!strcmp(a,"--cflags") || !strcmp(a,"--cflags-only-I")) cflags=1;
        else if (!strcmp(a,"--libs") || !strcmp(a,"--static")) libs=1;
        else if (a[0]!='-' && !module) module=a;
    }
    if (!module || !is_sodium(module)) return 1;
    if (exists) return 0;
    if (modver) { printf("1.0.22\n"); return 0; }
    if (cflags) { printf("-I$($SodiumInc.Replace('\','/'))\n"); return 0; }
    if (libs)   { printf("$($SodiumLib.Replace('\','/'))\n"); return 0; }
    return 0;
}
"@ | Set-Content -Path $FakeSrc -Encoding Ascii

$FakePkgConfig = Join-Path $WorkDir 'pkg-config.exe'
gcc -O2 -o $FakePkgConfig $FakeSrc

# ---------------------------------------------------------------------------
# 3. c-toxcore (clone + cmp submodule, static build).
# ---------------------------------------------------------------------------
$ToxSrc = Join-Path $WorkDir 'c-toxcore'
if (-not (Test-Path $ToxSrc)) {
    git clone --depth 1 https://github.com/TokTok/c-toxcore $ToxSrc
}
if (-not (Test-Path (Join-Path $ToxSrc 'third_party\cmp\cmp.c'))) {
    git clone --depth 1 https://github.com/TokTok/cmp (Join-Path $ToxSrc 'third_party\cmp')
}

$env:PATH = (Split-Path $FakePkgConfig) + ';' + $env:PATH
$ToxBuild = Join-Path $WorkDir 'c-toxcore_build'
cmake -G 'MinGW Makefiles' -S $ToxSrc -B $ToxBuild `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ `
    -DCMAKE_MAKE_PROGRAM=mingw32-make `
    -DENABLE_SHARED=OFF -DBUILD_SHARED_LIBS=OFF `
    -DBUILD_TOXAV=OFF -DDHT_BOOTSTRAP=OFF -DBOOTSTRAP_DAEMON=OFF `
    -DUNITTEST=OFF -DAUTOTEST=OFF -DBUILD_MISC_TESTS=OFF -DBUILD_FUN_UTILS=OFF
cmake --build $ToxBuild --target toxcore -- -j4

# ---------------------------------------------------------------------------
# 4. Vendor headers + static libs into ThirdParty/toxcore/.
# ---------------------------------------------------------------------------
$ToxcoreLib = Get-ChildItem $ToxBuild -Recurse -Filter 'libtoxcore.a' | Select-Object -First 1
if (-not $ToxcoreLib) { throw 'libtoxcore.a not produced' }

$OutInclude = Join-Path $OutDir 'include\tox'
$OutLib     = Join-Path $OutDir 'lib'
New-Item -ItemType Directory -Force -Path $OutInclude, $OutLib | Out-Null

Copy-Item (Join-Path $ToxSrc 'toxcore\tox.h')          $OutInclude
Copy-Item (Join-Path $ToxSrc 'toxcore\tox_options.h')  $OutInclude
Copy-Item (Join-Path $ToxSrc 'toxcore\tox_log_level.h') $OutInclude
Copy-Item $ToxcoreLib.FullName $OutLib
Copy-Item $SodiumLib $OutLib
Copy-Item (Join-Path $SodiumDir 'mingw64\include\*.h') (Join-Path $OutDir 'include') -Force

Write-Host "Done. Vendored into $OutDir"
