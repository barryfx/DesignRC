#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
version=$(sed -n 's/^project(DesignRC VERSION \([^ ]*\).*/\1/p' \
  "$project_root/CMakeLists.txt")
test -n "$version"
build_dir="$project_root/build/linux-release"
package_stage=$(mktemp -d)
trap 'rm -rf -- "$package_stage"' EXIT HUP INT TERM

cmake --preset linux-release -S "$project_root" -B "$build_dir"
cmake --build "$build_dir"
cpack --config "$build_dir/CPackConfig.cmake" \
  -G DEB -B "$package_stage"

(cd "$project_root" &&
  find . -mindepth 1 -maxdepth 1 \
    ! -name .git ! -name build ! -name dist ! -name out ! -name tmp \
    ! -name '$install' -print0 |
  tar --null -T - -czf "$package_stage/DesignRC-$version-source.tar.gz")
(cd "$package_stage" && sha256sum *.deb "DesignRC-$version-source.tar.gz" \
  > "DesignRC-$version-Linux-x64.sha256")

mkdir -p "$project_root/dist"
cp "$package_stage"/*.deb "$package_stage"/*.tar.gz \
  "$package_stage"/*.sha256 "$project_root/dist/"
