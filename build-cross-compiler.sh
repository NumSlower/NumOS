#!/bin/bash
# Cross-Compiler Build Script for x86_64-elf
# This builds GCC and binutils for bare-metal x86-64 development

set -e

# Configuration
export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"

# Versions (you can update these)
BINUTILS_VERSION=2.41
GCC_VERSION=13.2.0

# Create directories
mkdir -p $HOME/src
cd $HOME/src

echo "=== Building x86_64-elf cross-compiler ==="
echo "This will take 30-60 minutes depending on your system"
echo ""
echo "Installation directory: $PREFIX"
echo "Target: $TARGET"
echo ""

# Download binutils
if [ ! -f "binutils-$BINUTILS_VERSION.tar.xz" ]; then
    echo "Downloading binutils $BINUTILS_VERSION..."
    wget https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz
fi

# Download GCC
if [ ! -f "gcc-$GCC_VERSION.tar.xz" ]; then
    echo "Downloading GCC $GCC_VERSION..."
    wget https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz
fi

# Extract binutils
echo ""
echo "=== Building binutils ==="
rm -rf binutils-$BINUTILS_VERSION
tar -xf binutils-$BINUTILS_VERSION.tar.xz
mkdir -p build-binutils
cd build-binutils

../binutils-$BINUTILS_VERSION/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror

make -j$(nproc)
make install

cd ..

# Extract GCC
echo ""
echo "=== Building GCC ==="
rm -rf gcc-$GCC_VERSION
tar -xf gcc-$GCC_VERSION.tar.xz
mkdir -p build-gcc
cd build-gcc

../gcc-$GCC_VERSION/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages=c,c++ \
    --without-headers

make -j$(nproc) all-gcc
make -j$(nproc) all-target-libgcc
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