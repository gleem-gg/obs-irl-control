#!/usr/bin/env bash
# Builds and installs the plugin as a Flatpak extension for the Flathub OBS Studio.
set -euo pipefail
cd "$(dirname "$0")/.."
flatpak-builder --user --install --force-clean build-flatpak \
    flatpak/com.obsproject.Studio.Plugin.IrlControl.yaml
echo "Installed. Restart OBS Studio (flatpak run com.obsproject.Studio)."
