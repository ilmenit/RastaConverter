#_@echo_off
# Script to build RastaConverter on Windows systems using PowerShell
# Usage: ./build.ps1 [-Preset <name>] [-Config <Debug|Release>] [-Clean] [-CleanOnly] [-ExtraCmakeArgs <args>]

param(
	[string]$Preset,
	[string]$Config = "Release",
	[switch]$Clean,
	[switch]$CleanOnly,
	[string[]]$ExtraCmakeArgs
)

$ErrorActionPreference = "Stop"
$ScriptRoot = $PSScriptRoot

# --- Helper Functions ---
function Invoke-CommandWithExitCode($command, $arguments) {
	if ($null -eq $arguments) { $arguments = @() }
	$arguments = @($arguments)  # ensure array
	Write-Host "Executing: $command $($arguments -join ' ')"
	& $command @arguments
	if ($LASTEXITCODE -ne 0) {
		throw "Command failed with exit code ${LASTEXITCODE}: $command $($arguments -join ' ')"
	}
}

# --- 1. Setup & Initial Validation ---
Write-Host "--- RastaConverter PowerShell Build Script ---"

# Check for required tools
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
	Write-Error "CMake is required but not found in your PATH. Aborting."
	exit 1
}

# Default preset if not provided
if (-not $Preset) {
	$Preset = "win-msvc"
	Write-Host "No preset specified, defaulting to '$Preset'."
}

# --- 2. Parse Arguments and Environment Variables ---
switch -Wildcard ($Config.ToLower()) {
	"debug"          { $Config = "Debug" }
	"release"        { $Config = "Release" }
	"relwithdebinfo" { $Config = "RelWithDebInfo" }
	"minsizerel"     { $Config = "MinSizeRel" }
}

$env:AUTO_VCPKG = if ($env:AUTO_VCPKG) { $env:AUTO_VCPKG } else { "0" }
$env:DISABLE_VCPKG = if ($env:DISABLE_VCPKG) { $env:DISABLE_VCPKG } else { "0" }
$env:NONINTERACTIVE = if ($env:NONINTERACTIVE) { $env:NONINTERACTIVE } else { "0" }

$useVcpkg = 0
$binaryDir = Join-Path $ScriptRoot "out\build\$Preset"

# --- 3. Require modern CMake with presets ---
Write-Host "Detecting CMake version..."
$cmakeVersionString = (cmake --version | Select-Object -First 1)
$cmakeVersionMatch = [regex]::Match($cmakeVersionString, '(\d+)\.(\d+)\.(\d+)')
$cmakeMajor = [int]$cmakeVersionMatch.Groups[1].Value
$cmakeMinor = [int]$cmakeVersionMatch.Groups[2].Value
if (($cmakeMajor -lt 3) -or ($cmakeMajor -eq 3 -and $cmakeMinor -lt 21)) {
	Write-Error "This project requires CMake 3.21+ with presets. Please upgrade CMake."
	exit 1
}
Write-Host "CMake version $cmakeMajor.$cmakeMinor detected. Using presets."

# --- 4. vcpkg Integration Logic ---
function Prepare-Vcpkg {
	if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"))) {
		Write-Host "Using existing VCPKG_ROOT: $env:VCPKG_ROOT"
		return $true
	}
	$localVcpkgPath = Join-Path $ScriptRoot ".vcpkg"
	if (Test-Path (Join-Path $localVcpkgPath "scripts\buildsystems\vcpkg.cmake")) {
		$env:VCPKG_ROOT = $localVcpkgPath
		Write-Host "Using local vcpkg instance at $env:VCPKG_ROOT"
		return $true
	}
	
	if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
		Write-Warning "git not found, cannot bootstrap vcpkg automatically."
		return $false
	}

	Write-Host "Bootstrapping local vcpkg under .vcpkg..."
	git clone --depth 1 https://github.com/microsoft/vcpkg.git $localVcpkgPath
	$bootstrapScript = Join-Path $localVcpkgPath "bootstrap-vcpkg.bat"
	if (Test-Path $bootstrapScript) {
		& $bootstrapScript -disableMetrics
		$env:VCPKG_ROOT = $localVcpkgPath
		return $true
	} else {
		Write-Error "vcpkg bootstrap script not found."
		return $false
	}
}

if ((Test-Path (Join-Path $ScriptRoot "vcpkg.json")) -and $env:DISABLE_VCPKG -ne "1") {
	if (($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"))) -or $env:AUTO_VCPKG -eq "1") {
		if (Prepare-Vcpkg) {
			$useVcpkg = 1
			Write-Host "Automatically enabling vcpkg."
		}
	}
}

# --- 5. Configure Step ---
$singleConfigGen = ($Preset -match "-make") -or ($Preset -match "win-mingw-gcc")
$cmakeArgs = @()

$cmakeArgs += "--preset"
$cmakeArgs += $Preset

# note: presets govern generator/build type; single-config logic is handled in presets

if ($ExtraCmakeArgs) {
	$cmakeArgs += $ExtraCmakeArgs
}

function Run-Configure {
	param($vcpkgEnabled)
	
	$finalArgs = @($cmakeArgs)
	if ($vcpkgEnabled) {
		$toolchainFile = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
		$finalArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
		$finalArgs += "-DVCPKG_FEATURE_FLAGS=manifests"
	}

	$success = $false
	try {
		Write-Host "Configuring project..."
		$cacheFile = Join-Path $binaryDir 'CMakeCache.txt'
		if ($Clean -and (Test-Path -LiteralPath $cacheFile)) {
			Write-Host "Clean requested, removing build directory: $binaryDir"
			Remove-Item -Recurse -Force $binaryDir
		}
		Invoke-CommandWithExitCode "cmake" $finalArgs
		if (Test-Path -LiteralPath $cacheFile) {
			$success = $true
		}
	} catch {
		Write-Warning "CMake configuration failed: $($_.Exception.Message)"
	}
	return $success
}

$configureSuccess = Run-Configure($useVcpkg -eq 1)

if (-not $configureSuccess) {
	Write-Warning "Initial CMake configuration failed."
	if ((Test-Path "vcpkg.json") -and $env:DISABLE_VCPKG -ne "1" -and $useVcpkg -eq 0) {
		$shouldTryVcpkg = "n"
		if ($env:AUTO_VCPKG -eq "1" -or $env:NONINTERACTIVE -eq "1") {
			$shouldTryVcpkg = "y"
		} else {
			$shouldTryVcpkg = Read-Host "Dependencies may be missing. Attempt vcpkg fallback? [y/N]"
		}

		if ($shouldTryVcpkg -eq "y") {
			if (Prepare-Vcpkg) {
				Write-Host "Re-configuring with vcpkg toolchain..."
				if (Test-Path $binaryDir) { Remove-Item -Recurse -Force $binaryDir }
				$configureSuccess = Run-Configure($true)
			}
		}
	}
}

if (-not $configureSuccess) {
	Write-Error "CMake configuration failed. Aborting."
	exit 1
}

# --- 6. Build Step ---
Write-Host "--- Build Phase ---"

if ($Clean) {
	try {
		Write-Host "Cleaning project (config: $Config)..."
		Invoke-CommandWithExitCode "cmake" @("--build", $binaryDir, "--config", $Config, "--target", "clean")
	} catch {
		Write-Warning "Clean command failed, but continuing with build. Error: $($_.Exception.Message)"
	}
}

if ($CleanOnly) {
	Write-Host "CleanOnly specified. Build skipped."
	exit 0
}

try {
	Write-Host "Building project (config: $Config)..."
	Invoke-CommandWithExitCode "cmake" @("--build", $binaryDir, "--config", $Config)
	Write-Host -ForegroundColor Green "Build successful!"
} catch {
	Write-Error "Build failed. Error: $($_.Exception.Message)"
	exit 1
}

# --- 7. Final Message ---
$artifactDir = if ($singleConfigGen) { $binaryDir } else { Join-Path $binaryDir $Config }
Write-Host "Artifacts are located in: $artifactDir"
