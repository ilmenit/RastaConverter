#!/usr/bin/env bash

set -euo pipefail

# RastaConverter PGO automation for Linux (GCC/Clang)
# - Phase 1: configure+build instrumented, run scenarios to create profile data
# - Phase 2: merge profiles (if using LLVM) or use directly (GCC)
# - Phase 3: configure+build optimized with profile data

# Change working dir to repo root (this script is expected to sit there)
SCRIPT_DIR="$(dirname "$0")"
cd "$SCRIPT_DIR"
REPO_ROOT="$(pwd)"

echo "=== RastaConverter PGO (Linux) ==="
echo
echo "This script will:"
echo "  1) Configure and build an instrumented binary (with -fprofile-generate or -fprofile-instr-generate)"
echo "  2) Run multiple training scenarios (500K-4M evaluations and LAHC solutions scaled by thread count) to produce profile data"
echo "  3) Merge profiles (LLVM) or use directly (GCC)"
echo "  4) Configure and build an optimized binary (with -fprofile-use or -fprofile-instr-use)"
echo

# Default values
COMPILER=""
PROFILE_TYPE=""
PRESET_GEN=""
PRESET_USE=""
PROFDIR=""
RUNDIR_PREFERRED=""
RUNDIR_FALLBACK=""
RUNEXE="RastaConverter"
TESTIMG="test.jpg"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --compiler=*)
            COMPILER="${1#*=}"
            shift
            ;;
        --compiler)
            COMPILER="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [--compiler=gcc|clang] [test_image]"
            echo
            echo "Options:"
            echo "  --compiler=COMPILER    Use specific compiler (gcc, clang, icx)"
            echo "  --help, -h             Show this help message"
            echo
            echo "Arguments:"
            echo "  test_image             Path to test image (default: test.jpg)"
            echo
            echo "Examples:"
            echo "  $0                                    # Auto-detect compiler, use test.jpg"
            echo "  $0 --compiler=gcc examples/test.jpg   # Use GCC with specific image"
            echo "  $0 --compiler=clang                   # Use Clang with test.jpg"
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

# Auto-detect compiler if not specified
if [[ -z "$COMPILER" ]]; then
    if command -v clang >/dev/null 2>&1; then
        COMPILER="clang"
    elif command -v gcc >/dev/null 2>&1; then
        COMPILER="gcc"
    elif command -v icx >/dev/null 2>&1; then
        COMPILER="icx"
    else
        echo "[error] No suitable compiler found. Please install gcc, clang, or icx." >&2
        exit 1
    fi
fi

# Validate compiler
case "$COMPILER" in
    gcc|clang|icx)
        if ! command -v "$COMPILER" >/dev/null 2>&1; then
            echo "[error] Compiler '$COMPILER' not found in PATH." >&2
            exit 1
        fi
        ;;
    *)
        echo "[error] Unsupported compiler: $COMPILER. Use gcc, clang, or icx." >&2
        exit 1
        ;;
esac

# Set up compiler-specific configuration
case "$COMPILER" in
    gcc)
        PROFILE_TYPE="gcc"
        PRESET_GEN="linux-gcc-pgo-gen"
        PRESET_USE="linux-gcc-pgo-use"
        PROFDIR="$PWD/pgo/gcc"
        ;;
    clang)
        PROFILE_TYPE="llvm"
        PRESET_GEN="linux-clang-pgo-gen"
        PRESET_USE="linux-clang-pgo-use"
        PROFDIR="$PWD/pgo/clang"
        ;;
    icx)
        PROFILE_TYPE="llvm"
        PRESET_GEN="linux-icx-pgo-gen"
        PRESET_USE="linux-icx-pgo-use"
        PROFDIR="$PWD/pgo/icx"
        ;;
esac

RUNDIR_PREFERRED="$PWD/build/$PRESET_GEN/Release"
RUNDIR_FALLBACK="$PWD/build/$PRESET_GEN"

echo "[info] Using compiler: $COMPILER ($PROFILE_TYPE profile type)"
echo "[info] Test image: $TESTIMG"
echo "[info] PGO Profile Generation Preset: $PRESET_GEN"
echo "[info] PGO Profile Usage Preset: $PRESET_USE"
echo "[info] Profile Directory: $PROFDIR"

# Check tools
if ! command -v cmake >/dev/null 2>&1; then
    echo "[error] CMake not found in PATH. Please install CMake 3.21+ and add it to PATH." >&2
    exit 1
fi

if [[ "$PROFILE_TYPE" == "llvm" ]]; then
    # Try Intel's llvm-profdata first, then fall back to system one
    if [[ "$COMPILER" == "icx" ]] && [[ -f "/opt/intel/oneapi/compiler/2025.2/bin/compiler/llvm-profdata" ]]; then
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
fi

# Create profile directory
mkdir -p "$PROFDIR"

# Check if presets exist
if ! cmake --list-presets 2>/dev/null | grep -q "$PRESET_GEN"; then
    echo "[error] PGO preset '$PRESET_GEN' not found." >&2
    echo "[hint] Make sure CMakePresets.json contains the required PGO presets." >&2
    echo "[hint] Available presets:" >&2
    cmake --list-presets 2>/dev/null | grep -E "(gcc|clang|icx).*pgo" || true
    exit 1
fi

echo "[step] Configure (instrumented) preset=$PRESET_GEN"
if ! cmake --preset "$PRESET_GEN"; then
    echo "[error] Configure [gen] failed." >&2
    exit 1
fi

echo "[step] Build (instrumented)"
echo "[info] Compiler and build configuration:"
cmake -LA -N "build/$PRESET_GEN" 2>/dev/null | grep -i "CMAKE_C_COMPILER\|CMAKE_CXX_COMPILER\|CMAKE_BUILD_TYPE\|CMAKE_C_FLAGS\|CMAKE_CXX_FLAGS" | head -10
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
    if [[ -f "$PWD/$TESTIMG" ]]; then
        echo "[info] Copying $TESTIMG to run directory..."
        cp "$PWD/$TESTIMG" "$RUNDIR/$TESTIMG"
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
    
    if [[ "$PROFILE_TYPE" == "llvm" ]]; then
        LLVM_PROFILE_FILE="$profile_file" $cmd
    else
        # GCC PGO - profile data is written to current directory
        $cmd
    fi
    
    if [[ $? -ne 0 ]]; then
        echo "[error] Scenario '$scenario_name' failed with exit code $?." >&2
        return 1
    fi
    
    if [[ "$PROFILE_TYPE" == "llvm" && ! -f "$profile_file" ]]; then
        echo "[error] Expected profile not written: $profile_file" >&2
        return 1
    fi
}

# Run training scenarios
if [[ "$PROFILE_TYPE" == "llvm" ]]; then
    # LLVM PGO scenarios
    run_scenario "01 base" "$PROFDIR/rasta-01-base.profraw" "./$RUNEXE $TESTIMG /max_evals=500000 /s=1" || exit 1
    run_scenario "02 t4" "$PROFDIR/rasta-02-t4.profraw" "./$RUNEXE $TESTIMG /threads=4 /max_evals=2000000 /s=5000" || exit 1
    run_scenario "03 t8" "$PROFDIR/rasta-03-t8.profraw" "./$RUNEXE $TESTIMG /threads=8 /max_evals=4000000 /s=50000" || exit 1
    run_scenario "04 dual" "$PROFDIR/rasta-04-dual.profraw" "./$RUNEXE $TESTIMG /dual /max_evals=500000 /s=1" || exit 1
    run_scenario "05 dual t4" "$PROFDIR/rasta-05-dual-t4.profraw" "./$RUNEXE $TESTIMG /dual /threads=4 /max_evals=2000000 /s=5000" || exit 1
    run_scenario "06 dual t8" "$PROFDIR/rasta-06-dual-t8.profraw" "./$RUNEXE $TESTIMG /dual /threads=8 /max_evals=4000000 /s=50000" || exit 1
else
    # GCC PGO scenarios (profile data goes to current directory)
    run_scenario "01 base" "" "./$RUNEXE $TESTIMG /max_evals=500000 /s=1" || exit 1
    run_scenario "02 t4" "" "./$RUNEXE $TESTIMG /threads=4 /max_evals=2000000 /s=5000" || exit 1
    run_scenario "03 t8" "" "./$RUNEXE $TESTIMG /threads=8 /max_evals=4000000 /s=50000" || exit 1
    run_scenario "04 dual" "" "./$RUNEXE $TESTIMG /dual /max_evals=500000 /s=1" || exit 1
    run_scenario "05 dual t4" "" "./$RUNEXE $TESTIMG /dual /threads=4 /max_evals=2000000 /s=5000" || exit 1
    run_scenario "06 dual t8" "" "./$RUNEXE $TESTIMG /dual /threads=8 /max_evals=4000000 /s=50000" || exit 1
fi

cd "$REPO_ROOT"

# Handle profile merging for LLVM
if [[ "$PROFILE_TYPE" == "llvm" ]]; then
    # Collect all .profraw files
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
fi

echo "[step] Configure (use) preset=$PRESET_USE"
if ! cmake --preset "$PRESET_USE"; then
    echo "[error] Configure [use] failed." >&2
    exit 1
fi

echo "[step] Build (optimized)"
if ! cmake --build --preset "$PRESET_USE"; then
    echo "[error] Build [use] failed." >&2
    exit 1
fi

echo "[success] PGO build complete."
echo "[hint] Optimized artifacts may be under:"
echo "  - build/$PRESET_USE/Release/"
echo "  - build/$PRESET_USE/"
if [[ "$PROFILE_TYPE" == "llvm" ]]; then
    echo "[hint] Profiles: $PROFDIR/merged.profdata (raw files kept)"
else
    echo "[hint] GCC profile data: $RUNDIR/*.gcda, $RUNDIR/*.gcno"
fi

