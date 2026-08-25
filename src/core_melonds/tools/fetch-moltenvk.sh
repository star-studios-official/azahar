#!/bin/sh
# Fetches the official MoltenVK release and stages the universal
# libMoltenVK.dylib for bundling into melonDS.app (see the macOS
# bundle logic in src/frontend/qt_sdl/CMakeLists.txt, which looks in
# external/moltenvk first).
#
# Usage: tools/fetch-moltenvk.sh [version] [outdir] [archive_sha256]
set -eu

VERSION="${1:-v1.4.0}"
OUTDIR="${2:-external/moltenvk}"
SHA256="${3:-}"
URL="https://github.com/KhronosGroup/MoltenVK/releases/download/${VERSION}/MoltenVK-macos.tar"
DYLIB_NAME="libMoltenVK.dylib"
LICENSE_NAME="LICENSE-MoltenVK.txt"
NOTICE_NAME="NOTICE-MoltenVK.txt"
METADATA_NAME="MoltenVK-stage.properties"

if [ -z "$SHA256" ]; then
    if [ "$VERSION" = "v1.4.0" ]; then
        SHA256="f4feaf6a4988352de8e6d49874ccc1cd6a45021e3cb476e8531bef7ecc73e93a"
    else
        echo "error: pass the SHA-256 checksum when selecting MoltenVK $VERSION" >&2
        exit 1
    fi
fi

mkdir -p "$OUTDIR"

file_sha256()
{
    shasum -a 256 "$1" | awk '{print $1}'
}

metadata_value()
{
    sed -n "s/^$1=//p" "$OUTDIR/$METADATA_NAME"
}

staged_files_match()
{
    [ -f "$OUTDIR/$DYLIB_NAME" ] || return 1
    [ -f "$OUTDIR/$LICENSE_NAME" ] || return 1
    [ -f "$OUTDIR/$NOTICE_NAME" ] || return 1
    [ -f "$OUTDIR/$METADATA_NAME" ] || return 1

    [ "$(metadata_value VERSION)" = "$VERSION" ] || return 1
    [ "$(metadata_value ARCHIVE_SHA256)" = "$SHA256" ] || return 1
    [ "$(metadata_value DYLIB_SHA256)" = "$(file_sha256 "$OUTDIR/$DYLIB_NAME")" ] || return 1
    [ "$(metadata_value LICENSE_SHA256)" = "$(file_sha256 "$OUTDIR/$LICENSE_NAME")" ] || return 1
    [ "$(metadata_value NOTICE_SHA256)" = "$(file_sha256 "$OUTDIR/$NOTICE_NAME")" ] || return 1
}

if staged_files_match; then
    echo "$OUTDIR/$DYLIB_NAME already contains verified MoltenVK $VERSION" >&2
    echo "$OUTDIR/$DYLIB_NAME"
    exit 0
fi

if [ -e "$OUTDIR/$DYLIB_NAME" ] || [ -e "$OUTDIR/$METADATA_NAME" ]; then
    echo "Existing MoltenVK stage is incomplete or does not match $VERSION/$SHA256; refreshing it" >&2
fi

echo "Downloading MoltenVK $VERSION..." >&2
TMPDIR_MOLTENVK=$(mktemp -d "${TMPDIR:-/tmp}/melonds-moltenvk.XXXXXX")
trap 'rm -rf "$TMPDIR_MOLTENVK"' EXIT HUP INT TERM
ARCHIVE="$TMPDIR_MOLTENVK/MoltenVK-macos.tar"
EXTRACTED="$TMPDIR_MOLTENVK/extracted"
STAGED="$TMPDIR_MOLTENVK/staged"
mkdir -p "$EXTRACTED"
mkdir -p "$STAGED"

curl -fL "$URL" -o "$ARCHIVE"
printf '%s  %s\n' "$SHA256" "$ARCHIVE" | shasum -a 256 -c -
tar -x -f "$ARCHIVE" -C "$EXTRACTED"

# release archive layout has moved around between versions; just locate
# the macOS dynamic library wherever it lives
DYLIB=$(find "$EXTRACTED" -name "$DYLIB_NAME" \( -path '*macos*' -o -path '*macOS*' \) | head -1)
if [ -z "$DYLIB" ]; then
    echo "error: no macOS libMoltenVK.dylib in the release archive:" >&2
    find "$EXTRACTED" -name '*.dylib' >&2
    exit 1
fi

LICENSE=$(find "$EXTRACTED" -type f -path '*/MoltenVK/LICENSE' | head -1)
if [ -z "$LICENSE" ]; then
    echo "error: no MoltenVK license in the release archive" >&2
    exit 1
fi

UPSTREAM_NOTICE=$(find "$EXTRACTED" -type f -path '*/MoltenVK/NOTICE' | head -1)

cp "$DYLIB" "$STAGED/$DYLIB_NAME"
cp "$LICENSE" "$STAGED/$LICENSE_NAME"

if [ -n "$UPSTREAM_NOTICE" ]; then
    cp "$UPSTREAM_NOTICE" "$STAGED/$NOTICE_NAME"
else
    : > "$STAGED/$NOTICE_NAME"
fi

{
    if [ -n "$UPSTREAM_NOTICE" ]; then
        printf '\n'
    fi
    printf '%s\n' "melonDS MoltenVK binary modification notice"
    printf '%s\n' "--------------------------------------------"
    printf '%s\n' "Source: $URL"
    printf '%s\n' "Version: $VERSION"
    printf '%s\n' "Archive SHA-256: $SHA256"
    printf '\n%s\n' "When packaging melonDS.app, the Mach-O install name (LC_ID_DYLIB)"
    printf '%s\n' "of libMoltenVK.dylib is changed to @rpath/libMoltenVK.dylib and its"
    printf '%s\n' "code signature is replaced with an ad-hoc signature. No MoltenVK"
    printf '%s\n' "source code is changed by the melonDS packaging process."
} >> "$STAGED/$NOTICE_NAME"

DYLIB_SHA256=$(file_sha256 "$STAGED/$DYLIB_NAME")
LICENSE_SHA256=$(file_sha256 "$STAGED/$LICENSE_NAME")
NOTICE_SHA256=$(file_sha256 "$STAGED/$NOTICE_NAME")
{
    printf 'VERSION=%s\n' "$VERSION"
    printf 'ARCHIVE_SHA256=%s\n' "$SHA256"
    printf 'DYLIB_SHA256=%s\n' "$DYLIB_SHA256"
    printf 'LICENSE_SHA256=%s\n' "$LICENSE_SHA256"
    printf 'NOTICE_SHA256=%s\n' "$NOTICE_SHA256"
} > "$STAGED/$METADATA_NAME"

# Install metadata last. An interrupted refresh will fail validation on the
# next run instead of making a partially updated stage look current.
for FILE in "$DYLIB_NAME" "$LICENSE_NAME" "$NOTICE_NAME"; do
    cp "$STAGED/$FILE" "$OUTDIR/.$FILE.tmp.$$"
    mv -f "$OUTDIR/.$FILE.tmp.$$" "$OUTDIR/$FILE"
done
cp "$STAGED/$METADATA_NAME" "$OUTDIR/.$METADATA_NAME.tmp.$$"
mv -f "$OUTDIR/.$METADATA_NAME.tmp.$$" "$OUTDIR/$METADATA_NAME"

echo "$OUTDIR/$DYLIB_NAME"
