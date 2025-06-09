#!/usr/bin/env bash

set -e

echo "Building Hyprland Remote Desktop Portal..."

# Create build directory
mkdir -p build
cd build

# Configure with cmake
cmake ..

# Build the project
make -j$(nproc)

echo "✓ Build complete!"
echo "Executable: build/xdg-desktop-portal-hypr-remote"
echo ""
echo "To run:"
echo "  cd build && ./xdg-desktop-portal-hypr-remote" 
