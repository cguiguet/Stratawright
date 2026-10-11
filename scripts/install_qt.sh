#!/bin/bash
# Unified Qt installer via aqtinstall (macOS + Windows/Git-Bash + CI).
# Single source of truth for the pinned Qt version.
# Usage: ./scripts/install_qt.sh [--outputdir DIR]
set -e

QT_VERSION="6.8.0"
QT_HOST="mac"
QT_TARGET="desktop"
QT_ARCH="clang_64"
DEFAULT_OUTPUTDIR="${QT_INSTALL_DIR:-$HOME/Qt}"

OUTPUTDIR="$DEFAULT_OUTPUTDIR"
while [[ $# -gt 0 ]]; do
    case $1 in
        --outputdir) OUTPUTDIR="$2"; shift 2 ;;
        --help) echo "Usage: $0 [--outputdir DIR]"; exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

QT_DIR="$OUTPUTDIR/$QT_VERSION/macos"
if [ -f "$QT_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "Qt $QT_VERSION already installed at $QT_DIR, skipping."
    exit 0
fi

echo "Installing Qt $QT_VERSION ($QT_ARCH) to $OUTPUTDIR ..."
if ! command -v aqt &> /dev/null; then
    echo "Installing aqtinstall via pip..."
    python3 -m pip install --quiet aqtinstall
fi

# Qt Svg and Qt Tools ship with the base Qt 6 install: passing them to -m makes
# aqtinstall abort ("packages [...] were not found"), so no add-on modules are requested.
aqt install-qt "$QT_HOST" "$QT_TARGET" "$QT_VERSION" "$QT_ARCH" \
    --outputdir "$OUTPUTDIR"

echo "Qt installed at $QT_DIR"
echo "Configure with: CMAKE_PREFIX_PATH=\"$QT_DIR\" ./scripts/build.sh release"
