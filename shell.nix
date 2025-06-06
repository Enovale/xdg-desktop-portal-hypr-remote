{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    # Build tools
    cmake
    pkg-config
    gcc

    # System dependencies
    libffi
    systemd
    
    # Wayland dependencies
    wayland
    wayland.dev
    wayland-protocols
    wayland-scanner

    # Other dependencies
    libei
    sdbus-cpp_2

    # Additional development tools
    gdb
    valgrind
  ];

  # Ensure pkg-config can find all the libraries
  PKG_CONFIG_PATH = with pkgs; lib.makeSearchPath "lib/pkgconfig" [
    libffi.dev
    wayland.dev
    wayland-protocols
    libei
    sdbus-cpp_2
    systemd.dev
  ];

  shellHook = ''
    echo "🚀 Hyprland Remote Desktop development environment ready!"
    echo ""
    echo "Available dependencies:"
    echo "  ✓ cmake: $(cmake --version | head -1)"
    echo "  ✓ wayland: $(pkg-config --modversion wayland-client || echo 'checking...')"
    echo "  ✓ wayland-protocols: $(pkg-config --modversion wayland-protocols || echo 'installed')"
    echo "  ✓ libei: $(pkg-config --modversion libei-1.0 || echo 'installed')"
    echo "  ✓ sdbus-c++: $(pkg-config --modversion sdbus-c++ || echo 'installed')"
    echo "  ✓ systemd: $(pkg-config --modversion libsystemd || echo 'installed')"
    echo ""
    echo "To build: ./build.sh"
    echo "To clean: rm -rf build"
  '';
} 