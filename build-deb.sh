#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ARCH=""
CLEAN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -a|--arch)
            ARCH="$2"
            shift 2
            ;;
        clean)
            CLEAN=1
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [clean] [-a|--arch <ARCH>]"
            exit 1
            ;;
    esac
done

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning build artifacts..."
    rm -rf debian/.debhelper debian/threadpool debian/files \
           debian/*.debhelper.log debian/*.substvars debian/*.debhelper
    rm -rf obj-*-linux-gnu obj-*
    rm -f ../*.deb ../*.changes ../*.buildinfo ../*.dsc \
          ../*.tar.xz ../*.tar.gz ../*.upload
    fakeroot debian/rules clean 2>/dev/null || true
    echo "Clean done."
    exit 0
fi

BUILD_OPTS="noddebs"
ARCH_OPT=""
if [[ -n "$ARCH" ]]; then
    ARCH_OPT="-a$ARCH"
fi

VERSION=$(sed -n 's/^project(ThreadPool VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
echo "Building threadpool ${VERSION} DEB package..."

# Update changelog date to today
DEB_DATE=$(date -R)
sed -i "s/^ -- Alex Sandro <dev@manjunos\.com>  .*$/ -- Alex Sandro <dev@manjunos.com>  ${DEB_DATE}/" debian/changelog

fakeroot debian/rules clean 2>/dev/null || true
DEB_BUILD_OPTIONS="$BUILD_OPTS" dpkg-buildpackage -us -uc -b -j$(nproc) $ARCH_OPT

echo "DEB package built successfully."
echo "Packages:"
ls -la ../*.deb 2>/dev/null || true
