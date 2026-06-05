#!/bin/bash
# IRIS4 Build Script — portable, no hardcoded paths.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_tmp"

echo "=== IRIS4 Build ==="
echo "Source: ${SCRIPT_DIR}"
echo "Build:  ${BUILD_DIR}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${SCRIPT_DIR}" "$@"
cmake --build . -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

echo ""
echo "=== Build complete ==="
echo ""

# Install to standard plugin directories (macOS)
if [ -d "IRIS4_artefacts/VST3/IRIS4.vst3" ]; then
    mkdir -p ~/Library/Audio/Plug-Ins/VST3/
    cp -r IRIS4_artefacts/VST3/IRIS4.vst3 ~/Library/Audio/Plug-Ins/VST3/
    echo "Installed VST3 → ~/Library/Audio/Plug-Ins/VST3/IRIS4.vst3"
fi

if [ -d "IRIS4_artefacts/AU/IRIS4.component" ]; then
    mkdir -p ~/Library/Audio/Plug-Ins/Components/
    cp -r IRIS4_artefacts/AU/IRIS4.component ~/Library/Audio/Plug-Ins/Components/
    echo "Installed AU  → ~/Library/Audio/Plug-Ins/Components/IRIS4.component"
fi

# Clean up build directory
cd "${SCRIPT_DIR}"
rm -rf "${BUILD_DIR}"
echo "Build directory cleaned."
