#!/bin/sh
# Downloads libcurl (64-bit MinGW build) into third_party/curl and copies the
# DLL into bin/. Run it from the project folder (next to the Makefile).
#
# Usage (from the w64devkit shell, in the project folder):
#     sh setup.sh            # install if missing
#     sh setup.sh --force    # re-download and reinstall
#
# Set CURL_URL to use a specific build, e.g. a pinned version:
#     CURL_URL=https://curl.se/windows/dl-8.11.0_1/curl-8.11.0_1-win64-mingw.zip \
#         sh setup.sh
set -eu

cd "$(dirname "$0")"

DEST=third_party/curl
TMP=.curl_setup_tmp
URL="${CURL_URL:-https://curl.se/windows/latest.cgi?p=win64-mingw.zip}"

if [ "${1:-}" != "--force" ] && [ -f "$DEST/include/curl/curl.h" ]; then
    echo "libcurl is already installed in $DEST (use --force to reinstall)."
    mkdir -p bin
    cp "$DEST"/bin/libcurl*.dll bin/ 2>/dev/null || true
    echo "DLL copied to bin/."
    exit 0
fi

have() { command -v "$1" >/dev/null 2>&1; }

download() { # url, output file
    if have curl; then
        curl -fL --retry 3 -o "$2" "$1"
    elif have wget; then
        wget -O "$2" "$1"
    elif have powershell.exe; then
        powershell.exe -NoProfile -Command \
            "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -UseBasicParsing -Uri '$1' -OutFile '$2'"
    else
        echo "error: need curl, wget or PowerShell to download files." >&2
        return 1
    fi
}

extract() { # zip file, output directory
    mkdir -p "$2"
    if have unzip; then
        unzip -q -o "$1" -d "$2"
    elif have powershell.exe; then
        powershell.exe -NoProfile -Command \
            "Expand-Archive -Force -Path '$1' -DestinationPath '$2'"
    else
        echo "error: need unzip or PowerShell to extract the download." >&2
        return 1
    fi
}

cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT INT TERM

rm -rf "$TMP"
mkdir -p "$TMP"

echo "Downloading libcurl from $URL"
if ! download "$URL" "$TMP/curl.zip"; then
    echo "error: download failed. Check your internet connection, or download the" >&2
    echo "       64-bit MinGW zip from https://curl.se/windows/ yourself and extract it" >&2
    echo "       so that $DEST/include/curl/curl.h exists." >&2
    exit 1
fi

echo "Extracting..."
extract "$TMP/curl.zip" "$TMP/extract"

# The zip contains one top-level folder such as curl-8.x.x_x-win64-mingw/.
top=$(ls "$TMP/extract" | head -n 1)
if [ -z "$top" ] || [ ! -f "$TMP/extract/$top/include/curl/curl.h" ]; then
    echo "error: unexpected archive layout (no include/curl/curl.h found)." >&2
    echo "       Make sure the download is the win64-mingw build." >&2
    exit 1
fi

rm -rf "$DEST"
mkdir -p third_party
mv "$TMP/extract/$top" "$DEST"

if ! ls "$DEST"/bin/libcurl*.dll >/dev/null 2>&1; then
    echo "warning: no libcurl DLL found in $DEST/bin; dk.exe may fail to start." >&2
fi

# dk.exe needs the DLL next to it at run time.
mkdir -p bin
cp "$DEST"/bin/libcurl*.dll bin/ 2>/dev/null || true

echo
echo "libcurl installed in $DEST (DLL copied to bin/)"
echo "Next step - build with:"
echo "    make CURL_PREFIX=$DEST"
echo "Note: 'make clean' deletes bin/, so re-run this script (or copy"
echo "$DEST/bin/libcurl*.dll into bin/) after cleaning."
