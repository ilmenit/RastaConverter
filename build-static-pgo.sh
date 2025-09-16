#!/bin/bash

# RastaConverter Static PGO Build Script
# Creates a statically linked, PGO-optimized binary for easy distribution

set -e

echo "=== RastaConverter Static PGO Build ==="
echo
echo "This script will:"
echo "  1) Run PGO build with Intel compiler"
echo "  2) Create a static version using the PGO profile data"
echo "  3) Copy distribution files"
echo

# Check if PGO profile data exists
if [[ ! -f "pgo/icx/merged.profdata" ]]; then
    echo "[error] PGO profile data not found. Run './build-pgo.sh --compiler=icx' first."
    exit 1
fi

echo "[step] Building static PGO-optimized binary..."
cmake --preset linux-icx-pgo-static
cmake --build --preset linux-icx-pgo-static

echo "[step] Creating distribution package..."
DIST_DIR="dist/rastaconverter-static-$(date +%Y%m%d)"
mkdir -p "$DIST_DIR"

# Copy the static binary
cp build/linux-icx-pgo-static/Release/RastaConverter "$DIST_DIR/"

# Copy essential files
cp README.md ChangeLog.md help.txt "$DIST_DIR/"
cp -r Palettes "$DIST_DIR/"

# Copy Windows DLLs for cross-platform compatibility
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

echo "[success] Static PGO build complete!"
echo "[info] Distribution package created in: $DIST_DIR"
echo "[info] Binary: $DIST_DIR/RastaConverter"
echo "[info] Run with: $DIST_DIR/run.sh <image_file>"
echo
echo "[note] This binary is statically linked and should run on most Linux systems"
echo "       without requiring additional library installations."
