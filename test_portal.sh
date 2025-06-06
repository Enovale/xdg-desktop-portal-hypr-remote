#!/usr/bin/env bash

echo "🧪 Testing Hyprland Remote Desktop Portal (Development Mode)"
echo

# Build first
./build.sh || exit 1

echo "Setting up development D-Bus environment..."

# Create temporary directories for D-Bus services
mkdir -p ~/.local/share/dbus-1/services
mkdir -p ~/.local/share/xdg-desktop-portal/portals

# Create development service file (using .dev name to avoid conflicts)
cat > ~/.local/share/dbus-1/services/org.freedesktop.impl.portal.desktop.hyprland.dev.service << EOF
[D-BUS Service]
Name=org.freedesktop.impl.portal.desktop.hypr-remote-desktop
Exec=$(pwd)/build/hyprland-remote-desktop
EOF

# Create development portal file
cat > ~/.local/share/xdg-desktop-portal/portals/hypr-remote-desktop.portal << EOF
[portal]
DBusName=org.freedesktop.impl.portal.desktop.hypr-remote-desktop
Interfaces=org.freedesktop.impl.portal.RemoteDesktop
UseIn=hyprland
EOF

echo "✓ Development files created:"
echo "  Service: ~/.local/share/dbus-1/services/org.freedesktop.impl.portal.desktop.hyprland.dev.service"
echo "  Portal:  ~/.local/share/xdg-desktop-portal/portals/hyprland-dev.portal"
echo

# Test the portal
echo "Testing the portal directly..."
echo "Should see 'Portal registered on SESSION bus' if working!"
echo "Press Ctrl+C to stop"
echo

./build/hyprland-remote-desktop 
