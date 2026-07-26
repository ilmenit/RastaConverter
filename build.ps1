Param(
	[string]$Preset,
	[string]$Config = "Release",
	[switch]$NoGui,
	[switch]$NoLiveUi,
	[switch]$Clean,
	[switch]$CleanOnly,
	[string]$Compiler,
	[string[]]$Extra
)

$ErrorActionPreference = "Stop"

Write-Host "=== RastaConverter Build (PowerShell) ==="

if ($env:DEBUG_BUILD -eq "1" -or $env:debug_build -eq "1") {
	$DebugPreference = "Continue"
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
	Write-Error "CMake not found. Please install CMake >= 3.21 and add to PATH."
	exit 1
}

if (-not $Preset) {
	$Preset = "x64-release"
}

switch -Wildcard ($Config.ToLower()) {
	"debug"          { $Config = "Debug" }
	"release"        { $Config = "Release" }
	"relwithdebinfo" { $Config = "RelWithDebInfo" }
	"minsizerel"     { $Config = "MinSizeRel" }
}

# If a non-MSVC compiler is requested, prefer Ninja presets when a VS preset would be used
if ($Compiler -and ($Compiler.ToLower() -ne "msvc")) {
	if ($Preset -match '^(win32|x64)-(debug|release)$') {
		$Preset = if ($Config -eq 'Debug') { 'ninja-debug' } else { 'ninja-release' }
	}
}

$binaryDir = Join-Path $PSScriptRoot "build/$Preset"
if ($NoGui) { $binaryDir = "$binaryDir-nogui" }
$cfgArgs = @("--preset", $Preset, "-B", $binaryDir)
$liveUi = -not $NoGui -and -not $NoLiveUi
$cfgArgs += "-DBUILD_NO_GUI=$(if ($NoGui) { 'ON' } else { 'OFF' })"
$cfgArgs += "-DENABLE_LIVE_UI=$(if ($liveUi) { 'ON' } else { 'OFF' })"
if ($env:VCPKG_ROOT) {
	$toolchain = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
	if (Test-Path $toolchain) {
		$cfgArgs += @("-DCMAKE_TOOLCHAIN_FILE=$toolchain", "-DVCPKG_FEATURE_FLAGS=manifests")
	}
}
if ($Compiler) {
    switch ($Compiler.ToLower()) {
        "clang"     { $cfgArgs += @("-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++") }
        "clang-cl"  { $cfgArgs += @("-DCMAKE_C_COMPILER=clang-cl", "-DCMAKE_CXX_COMPILER=clang-cl") }
        "gcc"       { $cfgArgs += @("-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++") }
        "mingw"     { $cfgArgs += @("-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++") }
        "icx"       { $cfgArgs += @("-DCMAKE_C_COMPILER=icx", "-DCMAKE_CXX_COMPILER=icx") }
        default      { }
    }
}
if ($Extra) { $cfgArgs += $Extra }

if ($Clean -or $CleanOnly) {
	if (Test-Path $binaryDir) {
		Write-Host "[info] CLEAN: removing $binaryDir"
		Remove-Item -Recurse -Force $binaryDir
	}
}

if ($CleanOnly) {
	Write-Host "[success] CLEANONLY: $binaryDir has been removed."
	exit 0
}

Write-Host "[info] Configuring (preset=$Preset, config=$Config, nogui=$($NoGui.IsPresent), liveui=$liveUi$(if($Compiler){", compiler=$Compiler"})) ..."
if ($Compiler) {
    Write-Host "[info] Compiler: $Compiler"
} else {
    Write-Host "[info] Compiler: auto-detected from preset"
}
if ($Extra) {
    Write-Host "[info] Extra CMake args: $($Extra -join ' ')"
}
& cmake -S $PSScriptRoot @cfgArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "[error] Configuration failed." -ForegroundColor Red
    Write-Host "[hint] Try one of the following:" -ForegroundColor Yellow
    Write-Host "  - Provide paths in config.env: FREEIMAGE_DIR, SDL3_DIR, SDL3_TTF_DIR"
    Write-Host "  - OR install system packages:"
    Write-Host "      Ubuntu:   install FreeImage, SDL3, and SDL3_ttf development packages"
    Write-Host "      macOS:    brew install freeimage sdl3 sdl3_ttf"
    Write-Host "      Windows:  use vcpkg or vendor SDKs"
    Write-Host "  - With vcpkg: set VCPKG_ROOT then pass toolchain, e.g.:"
    Write-Host "      cmake --preset $Preset -DCMAKE_TOOLCHAIN_FILE=\"$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake\""
    Write-Host "  - You can run: cmake -P check_dependencies.cmake   to see discovery hints"
    exit 1
}

Write-Host "[info] Detected compiler information:"
$compilerInfo = & cmake -LA -N $binaryDir 2>$null | Select-String -Pattern "CMAKE_C_COMPILER|CMAKE_CXX_COMPILER|CMAKE_BUILD_TYPE|ENABLE_" | Select-Object -First 10
if ($compilerInfo) { $compilerInfo | ForEach-Object { Write-Host "[info] $_" } }

Write-Host "[info] Building ..."
& cmake --build $binaryDir --config $Config
if ($LASTEXITCODE -ne 0) {
	Write-Error "Build failed. See errors above."
	exit $LASTEXITCODE
}

if (Test-Path (Join-Path $binaryDir $Config)) {
	Write-Host "[success] Artifacts: $(Join-Path $binaryDir $Config)"
} else {
	Write-Host "[success] Artifacts: $binaryDir"
}
