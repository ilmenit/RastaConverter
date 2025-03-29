#!/bin/bash
# Script to set up environment variables for building RastaConverter

# Function to find a library using pkg-config
find_with_pkg_config() {
    local name=$1
    local pkg_name=$2
    
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists $pkg_name; then
        pkg-config --variable=prefix $pkg_name
    else
        echo ""
    fi
}

# Try to find libraries automatically
FREEIMAGE_AUTO=$(find_with_pkg_config "FreeImage" "freeimage")
SDL2_AUTO=$(find_with_pkg_config "SDL2" "sdl2")
SDL2_TTF_AUTO=$(find_with_pkg_config "SDL2_ttf" "SDL2_ttf")

# Prompt user for library paths
echo "Enter path to FreeImage library (leave empty for system location or '$FREEIMAGE_AUTO'): "
read FREEIMAGE_DIR
if [ -z "$FREEIMAGE_DIR" ]; then
    FREEIMAGE_DIR=$FREEIMAGE_AUTO
fi

echo "Enter path to SDL2 library (leave empty for system location or '$SDL2_AUTO'): "
read SDL2_DIR
if [ -z "$SDL2_DIR" ]; then
    SDL2_DIR=$SDL2_AUTO
fi

echo "Enter path to SDL2_ttf library (leave empty for system location or '$SDL2_TTF_AUTO'): "
read SDL2_TTF_DIR
if [ -z "$SDL2_TTF_DIR" ]; then
    SDL2_TTF_DIR=$SDL2_TTF_AUTO
fi

# Save to config.env for build scripts
cat > config.env << EOF
FREEIMAGE_DIR="$FREEIMAGE_DIR"
SDL2_DIR="$SDL2_DIR"
SDL2_TTF_DIR="$SDL2_TTF_DIR"
EOF

# Export variables for current session
export FREEIMAGE_DIR
export SDL2_DIR
export SDL2_TTF_DIR

# Add to ~/.bashrc for persistence if user wants
echo "Do you want to add these variables to your ~/.bashrc? (y/n)"
read ADD_TO_BASHRC

if [ "$ADD_TO_BASHRC" = "y" ] || [ "$ADD_TO_BASHRC" = "Y" ]; then
    echo "" >> ~/.bashrc
    echo "# RastaConverter environment variables" >> ~/.bashrc
    echo "export FREEIMAGE_DIR=\"$FREEIMAGE_DIR\"" >> ~/.bashrc
    echo "export SDL2_DIR=\"$SDL2_DIR\"" >> ~/.bashrc
    echo "export SDL2_TTF_DIR=\"$SDL2_TTF_DIR\"" >> ~/.bashrc
    
    echo "Environment variables added to ~/.bashrc"
    echo "Please run 'source ~/.bashrc' to apply changes to current shell"
else
    echo "Environment variables set for current session only."
    echo "Run 'source config.env' before building in new terminal sessions."
fi

echo "Environment setup complete."
echo "You can now build RastaConverter with any of the configurations."
