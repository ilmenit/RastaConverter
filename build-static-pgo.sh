#!/bin/bash

# RastaConverter Static PGO Build Script
# Creates a statically linked, PGO-optimized binary for easy distribution
# This script follows the same PGO workflow as build-pgo.sh

set -euo pipefail

# Change working dir to repo root
SCRIPT_DIR="$(dirname "$0")"
cd "$SCRIPT_DIR"
REPO_ROOT="$(pwd)"

echo "=== RastaConverter Static PGO Build ==="
echo
echo "This script will:"
echo "  1) Configure and build an instrumented static binary (with -fprofile-instr-generate)"
echo "  2) Run multiple training scenarios to produce profile data"
echo "  3) Merge profiles"
echo "  4) Configure and build an optimized static binary (with -fprofile-instr-use)"
echo "  5) Create a distribution package"
echo

# Default values
TESTIMG="test.jpg"
PROFDIR="$REPO_ROOT/pgo/icx"
PRESET_GEN="linux-icx-pgo-static-gen"
PRESET_USE="linux-icx-pgo-static"
RUNDIR_PREFERRED="$REPO_ROOT/build/$PRESET_GEN/Release"
RUNDIR_FALLBACK="$REPO_ROOT/build/$PRESET_GEN"
RUNEXE="RastaConverter"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --help|-h)
            echo "Usage: $0 [test_image]"
            echo
            echo "Options:"
            echo "  --help, -h             Show this help message"
            echo
            echo "Arguments:"
            echo "  test_image             Path to test image (default: test.jpg)"
            echo
            echo "Examples:"
            echo "  $0                           # Use test.jpg"
            echo "  $0 examples/test.jpg         # Use specific image"
            exit 0
            ;;
        -*)
            echo "[error] Unknown option: $1" >&2
            echo "Use --help for usage information" >&2
            exit 1
            ;;
        *)
            TESTIMG="$1"
            shift
            ;;
    esac
done

# Check tools
if ! command -v cmake >/dev/null 2>&1; then
    echo "[error] CMake not found in PATH. Please install CMake 3.21+ and add it to PATH." >&2
    exit 1
fi

if ! command -v icx >/dev/null 2>&1; then
    echo "[error] Intel compiler (icx) not found in PATH." >&2
    echo "[hint] Did you set up the Intel compiler environment?" >&2
    echo "  source /opt/intel/oneapi/setvars.sh" >&2
    exit 1
fi

# Try Intel's llvm-profdata first, then fall back to system one
if [[ -f "/opt/intel/oneapi/compiler/2025.2/bin/compiler/llvm-profdata" ]]; then
    LLVM_PROFDATA="/opt/intel/oneapi/compiler/2025.2/bin/compiler/llvm-profdata"
elif command -v llvm-profdata >/dev/null 2>&1; then
    LLVM_PROFDATA="llvm-profdata"
else
    echo "[error] llvm-profdata not found in PATH." >&2
    echo "[hint] Install LLVM tools: sudo apt install llvm (Ubuntu) or brew install llvm (macOS)" >&2
    exit 1
fi
LLVM_PROFDATA_VERSION=$($LLVM_PROFDATA --version 2>&1 || echo "unknown")
echo "[info] llvm-profdata: $LLVM_PROFDATA_VERSION"

# Create profile directory
mkdir -p "$PROFDIR"

# Check if presets exist
if ! cmake --list-presets 2>/dev/null | grep -q "$PRESET_GEN"; then
    echo "[error] PGO preset '$PRESET_GEN' not found." >&2
    echo "[hint] Make sure CMakePresets.json contains the required PGO presets." >&2
    exit 1
fi

echo "[info] Test image: $TESTIMG"
echo "[info] PGO Profile Generation Preset: $PRESET_GEN"
echo "[info] PGO Profile Usage Preset: $PRESET_USE"
echo "[info] Profile Directory: $PROFDIR"

echo "[step] Configure (instrumented) preset=$PRESET_GEN"
if ! cmake --preset "$PRESET_GEN"; then
    echo "[error] Configure [gen] failed." >&2
    exit 1
fi

echo "[step] Build (instrumented)"
echo "[info] Compiler and build configuration:"
cmake -LA -N "build/$PRESET_GEN" 2>/dev/null | grep -i "CMAKE_C_COMPILER\|CMAKE_CXX_COMPILER\|CMAKE_BUILD_TYPE\|CMAKE_C_FLAGS\|CMAKE_CXX_FLAGS\|BUILD_STATIC" | head -10
if ! cmake --build --preset "$PRESET_GEN"; then
    echo "[error] Build [gen] failed." >&2
    exit 1
fi

# Find the executable
RUNDIR="$RUNDIR_PREFERRED"
if [[ ! -f "$RUNDIR/$RUNEXE" ]]; then
    if [[ -f "$RUNDIR_FALLBACK/$RUNEXE" ]]; then
        RUNDIR="$RUNDIR_FALLBACK"
    else
        echo "[error] Could not locate $RUNEXE in:" >&2
        echo "  - $RUNDIR_PREFERRED" >&2
        echo "  - $RUNDIR_FALLBACK" >&2
        exit 1
    fi
fi
echo "[info] Run directory: $RUNDIR"

# Ensure test image exists next to the executable
if [[ ! -f "$RUNDIR/$TESTIMG" ]]; then
    if [[ -f "$REPO_ROOT/$TESTIMG" ]]; then
        echo "[info] Copying $TESTIMG to run directory..."
        cp "$REPO_ROOT/$TESTIMG" "$RUNDIR/$TESTIMG"
    else
        echo "[error] $TESTIMG not found in repo root or run dir. Provide an input image or pass a path as an argument." >&2
        echo "[hint] Example: $0 examples/test.jpg" >&2
        exit 1
    fi
fi

echo "[step] Run training scenarios (writing profile data into $PROFDIR)"
echo "[info] Training scenarios:"
echo "[info]   01 base: 500K evaluations, /s=1 (1 thread)"
echo "[info]   02 t4:   2M evaluations, /s=5000 (4 threads)"
echo "[info]   03 t8:   4M evaluations, /s=50000 (8 threads)"
echo "[info]   04 dual: 500K evaluations, /s=1 (1 thread, dual mode)"
echo "[info]   05 dual t4: 2M evaluations, /s=5000 (4 threads, dual mode)"
echo "[info]   06 dual t8: 4M evaluations, /s=50000 (8 threads, dual mode)"
cd "$RUNDIR"

# Function to run a training scenario
run_scenario() {
    local scenario_name="$1"
    local profile_file="$2"
    local cmd="$3"
    
    echo "[run] $scenario_name: $cmd"
    
    LLVM_PROFILE_FILE="$profile_file" eval "$cmd"
    
    if [[ $? -ne 0 ]]; then
        echo "[error] Scenario '$scenario_name' failed with exit code $?." >&2
        return 1
    fi
    
    if [[ ! -f "$profile_file" ]]; then
        echo "[error] Expected profile not written: $profile_file" >&2
        return 1
    fi
}

# Run training scenarios (LLVM/ICX PGO)
run_scenario "01 base" "$PROFDIR/rasta-01-base.profraw" "./$RUNEXE $TESTIMG /max_evals=500000 /s=1" || exit 1
run_scenario "02 t4" "$PROFDIR/rasta-02-t4.profraw" "./$RUNEXE $TESTIMG /threads=4 /max_evals=2000000 /s=5000" || exit 1
run_scenario "03 t8" "$PROFDIR/rasta-03-t8.profraw" "./$RUNEXE $TESTIMG /threads=8 /max_evals=4000000 /s=50000" || exit 1
run_scenario "04 dual" "$PROFDIR/rasta-04-dual.profraw" "./$RUNEXE $TESTIMG /dual /max_evals=500000 /s=1" || exit 1
run_scenario "05 dual t4" "$PROFDIR/rasta-05-dual-t4.profraw" "./$RUNEXE $TESTIMG /dual /threads=4 /max_evals=2000000 /s=5000" || exit 1
run_scenario "06 dual t8" "$PROFDIR/rasta-06-dual-t8.profraw" "./$RUNEXE $TESTIMG /dual /threads=8 /max_evals=4000000 /s=50000" || exit 1

cd "$REPO_ROOT"

# Merge profiles
profraw_files=()
for file in "$PROFDIR"/*.profraw; do
    if [[ -f "$file" ]]; then
        profraw_files+=("$file")
    fi
done

if [[ ${#profraw_files[@]} -eq 0 ]]; then
    echo "[error] No .profraw files found in $PROFDIR." >&2
    exit 1
fi

echo "[step] Merge profiles into merged.profdata"
echo "  Input: ${profraw_files[*]}"
if ! $LLVM_PROFDATA merge -output="$PROFDIR/merged.profdata" "${profraw_files[@]}"; then
    echo "[error] llvm-profdata merge failed." >&2
    exit 1
fi

echo "[step] Configure (optimized) preset=$PRESET_USE"
if ! cmake --preset "$PRESET_USE"; then
    echo "[error] Configure [use] failed." >&2
    exit 1
fi

echo "[step] Build (optimized)"
if ! cmake --build --preset "$PRESET_USE"; then
    echo "[error] Build [use] failed." >&2
    exit 1
fi

echo "[step] Creating distribution package..."
DIST_DIR="dist/rastaconverter-static-$(date +%Y%m%d)"
mkdir -p "$DIST_DIR"

# Copy the static binary
if [[ -f "build/$PRESET_USE/Release/$RUNEXE" ]]; then
    cp "build/$PRESET_USE/Release/$RUNEXE" "$DIST_DIR/"
elif [[ -f "build/$PRESET_USE/$RUNEXE" ]]; then
    cp "build/$PRESET_USE/$RUNEXE" "$DIST_DIR/"
else
    echo "[error] Optimized binary not found." >&2
    exit 1
fi

# Copy essential files
cp README.md ChangeLog.md help.txt "$DIST_DIR/"
cp -r Palettes "$DIST_DIR/"

# Copy Windows DLLs for cross-platform compatibility (if present)
if [[ -d "dlls" ]]; then
    cp -r dlls "$DIST_DIR/"
fi

# Create a simple run script
cat > "$DIST_DIR/run.sh" << 'EOF'
#!/bin/bash
# Simple launcher script for RastaConverter
exec "$(dirname "$0")/RastaConverter" "$@"
EOF
chmod +x "$DIST_DIR/run.sh"

echo
echo "[success] Static PGO build complete!"
echo "[info] Distribution package created in: $DIST_DIR"
echo "[info] Binary: $DIST_DIR/RastaConverter"
echo "[info] Run with: $DIST_DIR/run.sh <image_file>"
echo
echo "[note] This binary is statically linked and should run on most Linux systems"
echo "       without requiring additional library installations."
echo "[hint] Profiles: $PROFDIR/merged.profdata (raw files kept)"
