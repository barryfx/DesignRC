#!/bin/bash
# Run as root in a fresh Ubuntu/Fedora container with desktop test prerequisites.
# Tests deliberately install no DesignRC, Qt, OCCT, or ICU packages.
set -euo pipefail
image=$(realpath "${1:?Pass the AppImage to test}")
fixtures=$(realpath "${2:?Pass the tests/fixtures directory}")
source /etc/os-release
echo "Testing AppImage on $PRETTY_NAME"
if ldconfig -p | grep -Eq 'libQt6|libTKBRep'; then
  echo 'Test environment already contains application libraries' >&2
  exit 1
fi
id appimagecheck >/dev/null 2>&1 || useradd -m appimagecheck
work=$(mktemp -d '/tmp/DesignRC AppImage test.XXXXXX')
trap 'rm -rf -- "$work"' EXIT
cp "$image" "$work/DesignRC.AppImage"
chmod 755 "$work/DesignRC.AppImage"
chown -R appimagecheck:appimagecheck "$work"
run() { runuser -u appimagecheck -- env XDG_CONFIG_HOME="$work/settings" "$@"; }
# Exercise the runtime itself, not just an AppDir. Containers do not expose FUSE.
for mode in panel-chord joiner cancellation; do
  run env QT_QPA_PLATFORM=offscreen "$work/DesignRC.AppImage" \
    --appimage-extract-and-run --"$mode"-backend-regression
done
run env QT_QPA_PLATFORM=offscreen "$work/DesignRC.AppImage" --appimage-extract-and-run \
  --project-backend-regression "$fixtures/Curved_LE_TE.designrc"
(
  cd "$work"
  run ./DesignRC.AppImage --appimage-extract > extraction.log
)
appdir="$work/squashfs-root"
grep -Fq 'DesignRC 1.9.1 User Help' "$appdir/usr/lib/designrc/help/index.html"
test -s "$appdir/usr/lib/designrc/licenses/THIRD_PARTY_NOTICES.md"
test -s "$appdir/usr/lib/designrc/licenses/ubuntu-runtime/packages.tsv"
test -s "$appdir/usr/share/designrc/occt/resources/Shaders/Declarations.glsl"
while IFS= read -r -d '' elf; do
  if LD_LIBRARY_PATH="$appdir/usr/lib/designrc:$appdir/usr/lib" ldd "$elf" 2>&1 | grep 'not found'; then
    echo "Missing dependency for $elf" >&2
    exit 1
  fi
done < <(find "$appdir/usr/lib" -type f \( -name '*.so*' -o -name designrc \) -print0)
# A relocated extracted AppDir must also work, with no original install paths.
mv "$appdir" "$work/relocated AppDir"
run env QT_QPA_PLATFORM=offscreen "$work/relocated AppDir/AppRun" --joiner-backend-regression
# Start XCB under a real X server and confirm a visible top-level window.
cat > "$work/gui-check.sh" <<'GUI'
#!/bin/bash
set -euo pipefail
image=$1
log=$2
setsid env LIBGL_ALWAYS_SOFTWARE=1 QT_QPA_PLATFORM=xcb "$image" --appimage-extract-and-run > "$log" 2>&1 &
pid=$!
trap 'kill -- -"$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
for attempt in {1..30}; do
  kill -0 "$pid"
  if xwininfo -root -tree | grep -q '"DesignRC'; then
    sleep 5
    kill -0 "$pid"
    xwininfo -root -tree
    exit 0
  fi
  sleep 1
done
echo 'No DesignRC window appeared' >&2
exit 1
GUI
chmod 755 "$work/gui-check.sh"
run xvfb-run -a bash "$work/gui-check.sh" "$work/DesignRC.AppImage" "$work/gui.log"
cat "$work/gui.log"
if grep -Ei 'could not|failed|fatal|segmentation|Aborted|Fontconfig error' "$work/gui.log"; then
  echo 'GUI startup error' >&2
  exit 1
fi
sha256sum "$image"
echo "$PRETTY_NAME: AppImage runtime, bundled dependencies, resources, relocation, geometry regressions, and XCB GUI startup PASSED"
echo 'FUSE mounting was not tested: containers use the supported extract-and-run runtime mode.'
