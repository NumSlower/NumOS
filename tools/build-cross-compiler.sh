#!/bin/bash
# Cross-Compiler Build Script for x86_64-elf
# This builds GCC and binutils for bare-metal x86-64 development

set -euo pipefail

# Configuration
export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"

# Versions (you can update these)
BINUTILS_VERSION=2.41
GCC_VERSION=13.2.0

download_archive() {
    local url="$1"
    local archive="$2"
    local tmp_archive="${archive}.part"

    if [ -f "$archive" ] && tar -tf "$archive" >/dev/null 2>&1; then
        return
    fi

    if [ -f "$archive" ]; then
        echo "Removing incomplete archive: $archive"
        rm -f "$archive"
    fi

    rm -f "$tmp_archive"
    echo "Downloading $(basename "$archive")..."

    if command -v curl >/dev/null 2>&1; then
        curl --fail --location --retry 3 -o "$tmp_archive" "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget --tries=3 -O "$tmp_archive" "$url"
    else
        echo "Missing required host tool: curl or wget"
        exit 1
    fi

    if ! tar -tf "$tmp_archive" >/dev/null 2>&1; then
        echo "Downloaded archive is invalid: $archive"
        rm -f "$tmp_archive"
        exit 1
    fi

    mv "$tmp_archive" "$archive"
}

# Validate host tools before starting a long build.
if ! command -v makeinfo >/dev/null 2>&1; then
    echo "Missing required host tool: makeinfo"
    echo "Install Texinfo first, then rerun this script."
    echo "Ubuntu or Debian: sudo apt install texinfo"
    echo "Fedora: sudo dnf install texinfo"
    echo "Arch: sudo pacman -S texinfo"
    exit 1
fi

NPROC="$(nproc)"

# Create directories
mkdir -p "$HOME/src"
cd "$HOME/src"

echo "=== Building x86_64-elf cross-compiler ==="
echo "This will take 30-60 minutes depending on your system"
echo ""
echo "Installation directory: $PREFIX"
echo "Target: $TARGET"
echo ""

# Download source archives
download_archive \
    "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz" \
    "binutils-$BINUTILS_VERSION.tar.xz"
download_archive \
    "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz" \
    "gcc-$GCC_VERSION.tar.xz"

# Extract binutils
echo ""
echo "=== Building binutils ==="
rm -rf "binutils-$BINUTILS_VERSION" build-binutils
tar -xf "binutils-$BINUTILS_VERSION.tar.xz"
mkdir -p build-binutils
cd build-binutils

../"binutils-$BINUTILS_VERSION"/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror

make -j"$NPROC"
make install

cd ..

# Extract GCC
echo ""
echo "=== Building GCC ==="
rm -rf "gcc-$GCC_VERSION" build-gcc
tar -xf "gcc-$GCC_VERSION.tar.xz"
(
    cd "gcc-$GCC_VERSION"
    bash contrib/download_prerequisites
)
mkdir -p build-gcc
cd build-gcc

../"gcc-$GCC_VERSION"/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages=c,c++ \
    --without-headers

make -j"$NPROC" all-gcc
make -j"$NPROC" all-target-libgcc
make install-gcc
make install-target-libgcc

echo ""
echo "=== Cross-compiler installation complete! ==="
echo ""
echo "Add this to your ~/.bashrc or ~/.zshrc:"
echo "export PATH=\"\$HOME/opt/cross/bin:\$PATH\""
echo ""
echo "Then run: source ~/.bashrc"
echo ""
echo "Verify installation:"
echo "  x86_64-elf-gcc --version"
echo "  x86_64-elf-ld --version"
