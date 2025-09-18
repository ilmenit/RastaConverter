Param(
	[string]$Preset,
	[string]$Config = "Release",
	[switch]$NoGui,
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
$cfgArgs = @("--preset", $Preset)
if ($NoGui) { $cfgArgs += "-DBUILD_NO_GUI=ON" }
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

if ($Clean) {
	if (Test-Path $binaryDir) {
		Write-Host "[info] CLEAN: removing $binaryDir"
		Remove-Item -Recurse -Force $binaryDir
	}
}

Write-Host "[info] Configuring (preset=$Preset, config=$Config, nogui=$($NoGui.IsPresent)$(if($Compiler){", compiler=$Compiler"})) ..."
if ($Compiler) {
    Write-Host "[info] Compiler: $Compiler"
} else {
    Write-Host "[info] Compiler: auto-detected from preset"
}
if ($Extra) {
    Write-Host "[info] Extra CMake args: $($Extra -join ' ')"
}
& cmake -S . @cfgArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "[error] Configuration failed." -ForegroundColor Red
    Write-Host "[hint] Try one of the following:" -ForegroundColor Yellow
    Write-Host "  - Provide paths in config.env: FREEIMAGE_DIR, SDL2_DIR, SDL2_TTF_DIR"
    Write-Host "  - OR install system packages:"
    Write-Host "      Ubuntu:   sudo apt install libfreeimage-dev libsdl2-dev libsdl2-ttf-dev"
    Write-Host "      macOS:    brew install freeimage sdl2 sdl2_ttf"
    Write-Host "      Windows:  use vcpkg or vendor SDKs"
    Write-Host "  - With vcpkg: set VCPKG_ROOT then pass toolchain, e.g.:"
    Write-Host "      cmake --preset $Preset -DCMAKE_TOOLCHAIN_FILE=\"$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake\""
    Write-Host "  - You can run: cmake -P check_dependencies.cmake   to see discovery hints"
    exit 1
}

if ($CleanOnly) {
	Write-Host "[info] CLEANONLY requested, exiting after configure."
	Write-Host "[info] Detected compiler information:"
	$compilerInfo = & cmake -LA -N $binaryDir 2>$null | Select-String -Pattern "CMAKE_C_COMPILER|CMAKE_CXX_COMPILER|CMAKE_BUILD_TYPE|ENABLE_" | Select-Object -First 10
	if ($compilerInfo) { $compilerInfo | ForEach-Object { Write-Host "[info] $_" } }
	exit 0
}

Write-Host "[info] Detected compiler information:"
$compilerInfo = & cmake -LA -N $binaryDir 2>$null | Select-String -Pattern "CMAKE_C_COMPILER|CMAKE_CXX_COMPILER|CMAKE_BUILD_TYPE|ENABLE_" | Select-Object -First 10
if ($compilerInfo) { $compilerInfo | ForEach-Object { Write-Host "[info] $_" } }

Write-Host "[info] Building ..."
& cmake --build $binaryDir --config $Config

if (Test-Path (Join-Path $binaryDir $Config)) {
	Write-Host "[success] Artifacts: $(Join-Path $binaryDir $Config)"
} else {
	Write-Host "[success] Artifacts: $binaryDir"
}


