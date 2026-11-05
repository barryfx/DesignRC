#!/bin/bash
# Run with source mounted read-only at /workspace, a native Linux build volume
# at /build, and output at /output. Optionally mount OCCT 8.0 at /occt-source.
set -euo pipefail

source /etc/os-release
if [[ "$ID" != ubuntu || "$VERSION_ID" != 24.04 ]]; then
  echo 'This compatibility build must run inside Ubuntu 24.04.' >&2
  exit 1
fi
test -f /workspace/CMakeLists.txt
mkdir -p /build /output
jobs=${BUILD_JOBS:-8}
version=$(sed -n 's/^project(DesignRC VERSION \([^ ]*\).*/\1/p' /workspace/CMakeLists.txt)
test -n "$version"
revision=${DEBIAN_PACKAGE_RELEASE:-1}
[[ "$revision" =~ ^[1-9][0-9]*$ ]]
package_version="$version-$revision"

# Never reuse the host's OCCT installation: it may require a newer glibc.
# Copy source to the native Linux volume to avoid slow Windows bind-mount IO.
if [[ ! -f /build/occt-source/.designrc-source-ready ]]; then
  mkdir -p /build/occt-source
  if [[ -n ${OCCT_SOURCE_ARCHIVE:-} ]]; then
    tar -xf "$OCCT_SOURCE_ARCHIVE" -C /build/occt-source
  elif [[ -f /occt-source/CMakeLists.txt ]]; then
    cp -a /occt-source/. /build/occt-source/
  else
    curl --fail --location --retry 3 \
      https://codeload.github.com/Open-Cascade-SAS/OCCT/tar.gz/refs/tags/V8_0_0 \
      -o /build/occt-8.0.0-source.tar.gz
    tar -xzf /build/occt-8.0.0-source.tar.gz --strip-components=1 -C /build/occt-source
  fi
  touch /build/occt-source/.designrc-source-ready
fi
for component in MAJOR MINOR MAINTENANCE; do
  expected=0
  [[ "$component" != MAJOR ]] || expected=8
  grep -Eq "set *\\(OCC_VERSION_${component} +${expected} *\\)" \
    /build/occt-source/adm/cmake/version.cmake
done
if [[ -f /build/occt-8.0.0-source.tar.gz ]]; then
  sha256sum /build/occt-8.0.0-source.tar.gz > /output/OCCT-8.0.0-source.sha256
fi
cmake -S /build/occt-source -B /build/occt-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/build/occt-install \
  -DINSTALL_DIR_LAYOUT=Unix -DINSTALL_DIR_CMAKE=cmake \
  -DBUILD_MODULE_ApplicationFramework=ON \
  -DBUILD_MODULE_DataExchange=ON -DBUILD_MODULE_Draw=OFF \
  -DUSE_FREETYPE=ON -DUSE_OPENGL=ON -DUSE_XLIB=ON \
  -DUSE_FFMPEG=OFF -DUSE_FREEIMAGE=OFF -DUSE_TBB=OFF -DUSE_VTK=OFF
cmake --build /build/occt-build --parallel "$jobs"
cmake --install /build/occt-build

cmake -S /workspace -B /build/designrc -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DDESIGNRC_BUILD_TESTS=ON \
  -DDESIGNRC_LINUX_PACKAGE_GENERATOR=DEB \
  -DDESIGNRC_DEBIAN_PACKAGE_RELEASE="$revision" \
  -DOpenCASCADE_DIR=/build/occt-install/cmake
cmake --build /build/designrc --parallel "$jobs"
test_config=$(mktemp -d)
trap 'rm -rf -- "$test_config"' EXIT
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/build/occt-install/lib XDG_CONFIG_HOME="$test_config" \
  ctest --test-dir /build/designrc --parallel 2 --timeout 180 --output-on-failure \
    --output-log "/output/DesignRC-$package_version-Ubuntu-24.04-tests.log"
cpack --config /build/designrc/CPackConfig.cmake -G DEB -B /build/packages
cp "/build/packages/designrc_${package_version}_amd64.deb" /output/

cd /workspace
find . -mindepth 1 -maxdepth 1 \
  ! -name .git ! -name build ! -name dist ! -name out ! -name tmp \
  ! -name '$install' ! -name '*.lnk' -print0 |
  tar --null -T - -czf "/output/DesignRC-$package_version-source.tar.gz"
dpkg-query -W > "/output/DesignRC-$package_version-Ubuntu-24.04-build-packages.txt"
dpkg-deb -f "/output/designrc_${package_version}_amd64.deb" \
  > "/output/DesignRC-$package_version-Ubuntu-24.04-package-info.txt"
cd /output
sha256sum "designrc_${package_version}_amd64.deb" "DesignRC-$package_version-source.tar.gz" \
  > "DesignRC-$package_version-Linux-x64.sha256"
