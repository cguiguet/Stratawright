#!/bin/bash

# Stratawright Third-Party SDK Downloader Script
# Automatically fetches required third-party C++ libraries and plugin SDKs into src/third_party

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_DIR="$PROJECT_ROOT/src/third_party"

mkdir -p "$THIRD_PARTY_DIR"

echo "Setting up third-party SDKs in $THIRD_PARTY_DIR..."

clone_repo() {
    local folder_name="$1"
    local repo_url="$2"
    local target_dir="$THIRD_PARTY_DIR/$folder_name"

    if [ -d "$target_dir" ] && [ "$(ls -A "$target_dir")" ]; then
        echo "✓ $folder_name already exists, skipping."
    else
        echo "--> Cloning $folder_name from $repo_url..."
        git clone --depth 1 --recursive "$repo_url" "$target_dir"
        echo "✓ $folder_name setup complete."
    fi
}

# 1. RtAudio (Audio Driver HAL)
clone_repo "rtaudio" "https://github.com/thestk/rtaudio.git"

# 2. RtMidi (MIDI Driver HAL)
clone_repo "rtmidi" "https://github.com/thestk/rtmidi.git"

# 3. VST3 SDK (Steinberg VST3 Host/Plugin SDK)
clone_repo "VST3_SDK" "https://github.com/steinbergmedia/vst3sdk.git"

# 4. CLAP SDK (CLever Audio Plugin SDK)
clone_repo "CLAP_SDK" "https://github.com/free-audio/clap.git"

# 5. Eigen (Linear Algebra & FFT)
clone_repo "eigen-5.0.0" "https://gitlab.com/libeigen/eigen.git"

# 6. Rubber Band (Pitch Shifting & Time Stretching)
# Uses upstream source + a committed CMake overlay (cmake/rubberband/CMakeLists.txt)
clone_repo "rubberband-default" "https://github.com/breakfastquay/rubberband.git"
cp "$PROJECT_ROOT/cmake/rubberband/CMakeLists.txt" "$THIRD_PARTY_DIR/rubberband-default/CMakeLists.txt"

# 7. SoundTouch (Time Stretching & Jitter Buffer)
clone_repo "soundtouch" "https://codeberg.org/soundtouch/soundtouch.git"

# 8. ASIO SDK (Windows Low-Latency Audio Driver)
clone_repo "ASIOSDK" "https://github.com/audiosdk/asio.git"

# 9. GeneralUser GS (General MIDI SoundFont for the built-in GM Synth instrument)
# Pinned to a commit and verified, so every checkout plays the same sounds.
SOUNDFONT_DIR="$THIRD_PARTY_DIR/soundfonts"
SOUNDFONT="$SOUNDFONT_DIR/GeneralUser-GS.sf2"
SOUNDFONT_URL="https://github.com/mrbumpy409/GeneralUser-GS/raw/684543d5e5efaef08d02be50dcda8d552478fa60/GeneralUser-GS.sf2"
SOUNDFONT_SHA256="9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe"
if [ -f "$SOUNDFONT" ] && [ "$(shasum -a 256 "$SOUNDFONT" | cut -d' ' -f1)" = "$SOUNDFONT_SHA256" ]; then
    echo "✓ GeneralUser GS SoundFont already exists, skipping."
else
    echo "--> Downloading GeneralUser GS SoundFont (32 MB)..."
    mkdir -p "$SOUNDFONT_DIR"
    curl -fL --retry 3 -o "$SOUNDFONT.download" "$SOUNDFONT_URL"
    if [ "$(shasum -a 256 "$SOUNDFONT.download" | cut -d' ' -f1)" != "$SOUNDFONT_SHA256" ]; then
        rm -f "$SOUNDFONT.download"
        echo "✗ GeneralUser GS checksum mismatch, aborting." >&2
        exit 1
    fi
    mv "$SOUNDFONT.download" "$SOUNDFONT"
    echo "✓ GeneralUser GS SoundFont setup complete."
fi

echo ""
echo "✓ All third-party SDKs are successfully configured in $THIRD_PARTY_DIR!"
