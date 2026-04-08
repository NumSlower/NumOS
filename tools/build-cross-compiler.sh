#!/bin/bash
# Cross-Compiler Build Script
# Supports: x86_64-elf, aarch64-elf, aarch64-linux-gnu, amd64 (x86_64 alias)

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
NUMOS_VERSION="$(tr -d '\r\n' < "$REPO_ROOT/VERSION" 2>/dev/null || echo "v0.0.0")"

# Versions
BINUTILS_VERSION=2.41
GCC_VERSION=13.2.0

# Architecture presets
declare -A ARCH_TARGETS=(
    [x86_64]="x86_64-elf"
    [amd64]="x86_64-elf"
    [arm64]="aarch64-elf"
    [aarch64]="aarch64-elf"
    [arm]="arm-eabi"
    [armhf]="arm-linux-gnueabihf"
    [aarch64-linux]="aarch64-linux-gnu"
)

declare -A ARCH_LANGUAGES=(
    [x86_64-elf]="c,c++"
    [aarch64-elf]="c,c++"
    [arm-eabi]="c,c++"
    [arm-linux-gnueabihf]="c,c++"
    [aarch64-linux-gnu]="c,c++"
)

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -a, --arch ARCH     Target architecture, default: detected host arch"
    echo "  -t, --target TARGET Full target triple (overrides --arch)"
    echo "  -p, --prefix PATH   Installation prefix (default: \$HOME/opt/cross)"
    echo "  -s, --source-dir DIR Source and archive directory (default: \$HOME/src)"
    echo "  -j, --jobs N        Parallel jobs (default: nproc)"
    echo "      --fetch-only    Download source archives and stop"
    echo "  --list-archs        List supported architecture shortcuts"
    echo "  -v, --version       Show script version"
    echo "  -h, --help          Show this help"
    echo ""
    echo "Supported arch shortcuts:"
    for arch in "${!ARCH_TARGETS[@]}"; do
        printf "  %-16s -> %s\n" "$arch" "${ARCH_TARGETS[$arch]}"
    done | sort
    exit 0
}

list_archs() {
    echo "Supported architecture shortcuts:"
    echo ""
    for arch in "${!ARCH_TARGETS[@]}"; do
        printf "  %-16s -> %s\n" "$arch" "${ARCH_TARGETS[$arch]}"
    done | sort
    echo ""
    echo "You can also pass any valid GCC target triple directly with --target."
    exit 0
}

# Defaults
detect_default_arch() {
    local host_arch
    host_arch="$(uname -m 2>/dev/null || echo unknown)"
    case "$host_arch" in
        x86_64|amd64)
            echo "x86_64"
            ;;
        aarch64|arm64)
            echo "arm64"
            ;;
        armv7l|armv7*|armhf)
            echo "armhf"
            ;;
        armv6l|armv6*)
            echo "arm"
            ;;
        *)
            echo "x86_64"
            ;;
    esac
}

ARCH="$(detect_default_arch)"
TARGET=""
PREFIX="$HOME/opt/cross"
SOURCE_DIR="$HOME/src"
FETCH_ONLY=0
NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -a|--arch)
            ARCH="$2"
            shift 2
            ;;
        -t|--target)
            TARGET="$2"
            shift 2
            ;;
        -p|--prefix)
            PREFIX="$2"
            shift 2
            ;;
        -s|--source-dir)
            SOURCE_DIR="$2"
            shift 2
            ;;
        -j|--jobs)
            NPROC="$2"
            shift 2
            ;;
        --fetch-only)
            FETCH_ONLY=1
            shift
            ;;
        --list-archs)
            list_archs
            ;;
        -v|--version)
            echo "build-cross-compiler.sh $NUMOS_VERSION"
            exit 0
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# Resolve target triple
if [ -z "$TARGET" ]; then
    if [[ -v ARCH_TARGETS[$ARCH] ]]; then
        TARGET="${ARCH_TARGETS[$ARCH]}"
    else
        echo "Unknown arch: $ARCH"
        echo "Use --list-archs to see supported shortcuts, or pass a full triple with --target."
        exit 1
    fi
fi

# Resolve languages for target
LANGUAGES="${ARCH_LANGUAGES[$TARGET]:-c,c++}"

export PREFIX
export TARGET
export PATH="$PREFIX/bin:$PATH"

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

# Validate host tools
check_tool() {
    local tool="$1"
    local hint="$2"
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required host tool: $tool"
        echo "$hint"
        exit 1
    fi
}

check_tool makeinfo \
    "Install Texinfo first. Ubuntu/Debian: sudo apt install texinfo | Fedora: sudo dnf install texinfo | Arch: sudo pacman -S texinfo"

# For ARM targets, confirm required multilib/arm support on host
if [[ "$TARGET" == arm* ]] && ! gcc -dumpmachine 2>/dev/null | grep -q arm; then
    if ! dpkg -l gcc-arm-linux-gnueabihf >/dev/null 2>&1 && \
       ! command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then
        echo "Note: Building ARM cross-compiler from an x86 host."
        echo "If the build fails, install: sudo apt install gcc-arm-linux-gnueabihf"
    fi
fi

# Setup
mkdir -p "$SOURCE_DIR"
cd "$SOURCE_DIR"

echo "=== Building $TARGET cross-compiler ==="
echo "This will take 30-60 minutes depending on your system"
echo ""
echo "Detected host arch:     $(uname -m 2>/dev/null || echo unknown)"
echo "Installation directory: $PREFIX"
echo "Source directory:       $SOURCE_DIR"
echo "Target:                 $TARGET"
echo "Languages:              $LANGUAGES"
echo "Parallel jobs:          $NPROC"
echo ""

# Download
download_archive \
    "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz" \
    "binutils-$BINUTILS_VERSION.tar.xz"
download_archive \
    "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz" \
    "gcc-$GCC_VERSION.tar.xz"

if [[ "$FETCH_ONLY" == "1" ]]; then
    echo ""
    echo "=== Source download complete ==="
    echo "binutils archive: $SOURCE_DIR/binutils-$BINUTILS_VERSION.tar.xz"
    echo "gcc archive:      $SOURCE_DIR/gcc-$GCC_VERSION.tar.xz"
    exit 0
fi

# Build binutils
echo ""
echo "=== Building binutils ==="
rm -rf "binutils-$BINUTILS_VERSION" "build-binutils-$TARGET"
tar -xf "binutils-$BINUTILS_VERSION.tar.xz"
mkdir -p "build-binutils-$TARGET"
cd "build-binutils-$TARGET"

../"binutils-$BINUTILS_VERSION"/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror

make -j"$NPROC"
make install

cd ..

# Build GCC
echo ""
echo "=== Building GCC ==="
rm -rf "gcc-$GCC_VERSION" "build-gcc-$TARGET"
tar -xf "gcc-$GCC_VERSION.tar.xz"
(
    cd "gcc-$GCC_VERSION"
    bash contrib/download_prerequisites
)
mkdir -p "build-gcc-$TARGET"
cd "build-gcc-$TARGET"

GCC_CONFIGURE_EXTRA=""

# Target-specific configure flags
case "$TARGET" in
    aarch64-elf)
        GCC_CONFIGURE_EXTRA="--without-headers --with-newlib --disable-shared --disable-threads"
        ;;
    arm-eabi)
        GCC_CONFIGURE_EXTRA="--without-headers --with-newlib --disable-shared --disable-threads --with-arch=armv7-a --with-mode=thumb"
        ;;
    arm-linux-gnueabihf)
        GCC_CONFIGURE_EXTRA="--with-float=hard --with-fpu=vfpv3-d16 --with-arch=armv7-a"
        ;;
    aarch64-linux-gnu)
        GCC_CONFIGURE_EXTRA=""
        ;;
    x86_64-elf)
        GCC_CONFIGURE_EXTRA="--without-headers"
        ;;
esac

# shellcheck disable=SC2086
../"gcc-$GCC_VERSION"/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages="$LANGUAGES" \
    $GCC_CONFIGURE_EXTRA

make -j"$NPROC" all-gcc
make -j"$NPROC" all-target-libgcc
make install-gcc
make install-target-libgcc

echo ""
echo "=== Cross-compiler installation complete! ==="
echo ""
echo "Add this to your ~/.bashrc or ~/.zshrc:"
echo "  export PATH=\"\$HOME/opt/cross/bin:\$PATH\""
echo ""
echo "Then run: source ~/.bashrc"
echo ""
echo "Verify installation:"
echo "  ${TARGET}-gcc --version"
echo "  ${TARGET}-ld --version"
