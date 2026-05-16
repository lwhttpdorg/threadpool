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

VERSION=$(sed -n 's/^project(ThreadPool VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning build artifacts..."
    rm -f ~/rpmbuild/SPECS/libthreadpool.spec
    rm -f ~/rpmbuild/SOURCES/libthreadpool-*.tar.gz
    rm -rf ~/rpmbuild/BUILD/libthreadpool-*
    rm -rf ~/rpmbuild/BUILDROOT/libthreadpool-*
    rm -f ~/rpmbuild/RPMS/noarch/libthreadpool-devel-*.rpm
    rm -f ~/rpmbuild/RPMS/*/libthreadpool-devel-*.rpm
    rm -f ~/rpmbuild/SRPMS/libthreadpool-*.rpm
    echo "Clean done."
    exit 0
fi

# Setup rpmbuild tree if not exists
if [[ ! -d ~/rpmbuild ]]; then
    rpmdev-setuptree
fi

echo "Building threadpool ${VERSION} RPM package..."

# Create tarball from parent directory with proper top-level directory name
cd ..
tar -czf ~/rpmbuild/SOURCES/libthreadpool-${VERSION}.tar.gz \
    --transform "s,^threadpool,libthreadpool-${VERSION}," \
    --exclude='.git' --exclude='build*' --exclude='meson-build-*' \
    --exclude='cmake-build-*' --exclude='obj-*' --exclude='debian' \
    --exclude='*.deb' --exclude='*.changes' --exclude='*.buildinfo' \
    threadpool/

cd "$SCRIPT_DIR"

# Generate spec from template
sed -e "s/@VERSION@/${VERSION}/g" \
    -e "s/@DATE@/$(date +"%a %b %d %Y")/g" \
    libthreadpool.spec.in > ~/rpmbuild/SPECS/libthreadpool.spec

# Build RPM
if [[ -n "$ARCH" ]]; then
    rpmbuild -ba --target "$ARCH" ~/rpmbuild/SPECS/libthreadpool.spec
else
    rpmbuild -ba ~/rpmbuild/SPECS/libthreadpool.spec
fi

echo "RPM package built successfully."
echo "Binary packages:"
find ~/rpmbuild/RPMS -name "libthreadpool-devel-*.rpm" 2>/dev/null || true
echo "Source packages:"
find ~/rpmbuild/SRPMS -name "libthreadpool-*.rpm" 2>/dev/null || true
