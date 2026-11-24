#!/bin/bash
# Package the Ubuntu 24.04 release DEB without rebuilding its tested executable.
# Official packaging tools are cached and verified against pinned checksums.
set -euo pipefail
source /etc/os-release
[[ "$ID" = ubuntu && "$VERSION_ID" = 24.04 && $(uname -m) = x86_64 ]]
package=$(realpath "${1:?Pass the Ubuntu 24.04 release DEB}")
output=$(realpath -m "${2:-/output}")
tools=${APPIMAGE_TOOLS_DIR:-/build/appimage-tools}
source_dir=$(cd -- "$(dirname -- "$0")/../.." && pwd)
version=$(dpkg-deb -f "$package" Version)
version=${version%%-*}
mkdir -p "$tools"
for tool in linuxdeploy.AppImage appimagetool.AppImage runtime-x86_64; do
  if [[ ! -f "$tools/$tool" ]]; then
    case "$tool" in
      linuxdeploy.AppImage) url=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage ;;
      appimagetool.AppImage) url=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage ;;
      runtime-x86_64) url=https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64 ;;
    esac
    curl --fail --location --retry 3 "$url" -o "$tools/$tool"
  fi
done
# Continuous download URLs can change: reject unreviewed tool/runtime binaries.
(cd "$tools" && sha256sum -c "$source_dir/packaging/linux/appimage-tools.sha256")
chmod +x "$tools/"*.AppImage
mkdir -p "$output"
stage=$(mktemp -d /build/designrc-appimage.XXXXXX)
trap 'rm -rf -- "$stage"' EXIT
appdir="$stage/DesignRC.AppDir"
mkdir -p "$appdir"
dpkg-deb -x "$package" "$appdir"
install -m755 "$source_dir/packaging/linux/AppRun" "$appdir/AppRun"
# The DEB wrapper intentionally uses system paths; it is not used by AppRun.
rm "$appdir/usr/bin/designrc"
ln -s ../lib/designrc/designrc "$appdir/usr/bin/designrc"
sed 's|Icon=/usr/share/designrc/graphics/designrc_icon.png|Icon=designrc|' \
  "$appdir/usr/share/applications/designrc.desktop" > "$appdir/designrc.desktop"
cp "$appdir/designrc.desktop" "$appdir/usr/share/applications/designrc.desktop"
cp "$appdir/usr/share/designrc/graphics/designrc_icon.png" "$appdir/designrc.png"
mkdir -p "$appdir/usr/share/icons/hicolor/256x256/apps"
cp "$appdir/designrc.png" "$appdir/usr/share/icons/hicolor/256x256/apps/designrc.png"
ln -s designrc.png "$appdir/.DirIcon"
# Qt loads these dynamically, so ELF dependency scanning alone cannot find them.
qt_plugins=$(qmake6 -query QT_INSTALL_PLUGINS)
if [[ -d "$qt_plugins/xcbglintegrations" ]]; then
  cp -a "$qt_plugins/xcbglintegrations" "$appdir/usr/lib/designrc/qt6/plugins/"
fi
cp -aL /etc/fonts "$appdir/usr/share/designrc/fonts"
export APPIMAGE_EXTRACT_AND_RUN=1
export LD_LIBRARY_PATH="$appdir/usr/lib/designrc${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$tools/linuxdeploy.AppImage" --appdir "$appdir" \
  --deploy-deps-only "$appdir/usr/lib/designrc" \
  --library /usr/lib/x86_64-linux-gnu/libOpenGL.so.0 \
  --library /usr/lib/x86_64-linux-gnu/libfontconfig.so.1 \
  --library /usr/lib/x86_64-linux-gnu/libharfbuzz.so.0 \
  --custom-apprun "$source_dir/packaging/linux/AppRun"

# Preserve distribution copyright/license notices for the additional libraries.
notices="$appdir/usr/lib/designrc/licenses/ubuntu-runtime"
mkdir -p "$notices"
while IFS= read -r library; do
  name=$(basename "$library")
  original=$(ldconfig -p | awk -v name="$name" '$1 == name {print $NF; exit}')
  [[ -n "$original" ]] || continue
  owner=$(dpkg-query -S "$original" 2>/dev/null | head -1 | sed 's/: \/.*//') || true
  if [[ -z "$owner" ]]; then
    owner=$(dpkg-query -S "$(realpath "$original")" 2>/dev/null | head -1 | sed 's/: \/.*//') || true
  fi
  [[ -n "$owner" ]] || { echo "No package owner for $original" >&2; exit 1; }
  copyright="/usr/share/doc/${owner%%:*}/copyright"
  test -s "$copyright"
  cp -L "$copyright" "$notices/${owner%%:*}.copyright"
  printf '%s\t%s\t%s\n' "$name" "$owner" "$(dpkg-query -W -f='${Version}' "$owner")"
done < <(find "$appdir/usr/lib" -type f -name '*.so*' | sort) > "$notices/packages.tsv"
cp -a /usr/share/common-licenses "$notices/"
(
  cd "$tools"
  sha256sum linuxdeploy.AppImage appimagetool.AppImage runtime-x86_64
) > "$output/DesignRC-$version-AppImage-tools.sha256"
find "$appdir" -type f -printf '%P\n' | sort > "$output/DesignRC-$version-AppImage-files.txt"
export ARCH=x86_64 VERSION="$version"
"$tools/appimagetool.AppImage" --runtime-file "$tools/runtime-x86_64" \
  "$appdir" "$output/DesignRC-$version-x86_64.AppImage"
(
  cd "$output"
  sha256sum "DesignRC-$version-x86_64.AppImage" > "DesignRC-$version-AppImage.sha256"
)
echo "Created $output/DesignRC-$version-x86_64.AppImage"
