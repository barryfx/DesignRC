#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
package_stage=$(mktemp -d)
trap 'rm -rf -- "$package_stage"' EXIT HUP INT TERM

cmake --preset linux-release -S "$project_root"
cmake --build --preset linux-release
cpack --config "$project_root/build/linux-release/CPackConfig.cmake" \
  -G DEB -B "$package_stage"

tar --exclude=.git --exclude=build --exclude=dist --exclude=out \
  --exclude=tmp --exclude='$install' \
  -czf "$package_stage/DesignRC-0.9.0-source.tar.gz" \
  -C "$project_root" .
(cd "$package_stage" && sha256sum *.deb DesignRC-0.9.0-source.tar.gz \
  > DesignRC-0.9.0-Linux-x64.sha256)

mkdir -p "$project_root/dist"
cp "$package_stage"/*.deb "$package_stage"/*.tar.gz \
  "$package_stage"/*.sha256 "$project_root/dist/"
