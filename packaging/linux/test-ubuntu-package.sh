#!/bin/bash
# Run as root in a disposable Ubuntu container with /workspace mounted read-only.
set -euo pipefail
package=${1:?Pass the absolute path of the DEB to test}
version=$(dpkg-deb -f "$package" Version)
application_version=${version%%-*}
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends "$package" xvfb xauth libgl1-mesa-dri
test "$(dpkg-query -W -f='${Version}' designrc)" = "$version"
grep -Fq "DesignRC $application_version User Help" /usr/lib/designrc/help/index.html
test -s /usr/lib/designrc/licenses/DESIGNRC-GPL-3.0.txt
test -s /usr/lib/designrc/licenses/THIRD_PARTY_NOTICES.md
test -s /usr/share/applications/designrc.desktop
test -s /usr/share/designrc/graphics/designrc_icon.png
if LD_LIBRARY_PATH=/usr/lib/designrc ldd /usr/lib/designrc/designrc | grep 'not found'; then
  echo 'Missing runtime dependency' >&2
  exit 1
fi
id releasecheck >/dev/null 2>&1 || useradd -m releasecheck
for mode in panel-chord joiner cancellation; do
  runuser -u releasecheck -- env QT_QPA_PLATFORM=offscreen \
    /usr/bin/designrc --"$mode"-backend-regression
done
runuser -u releasecheck -- env QT_QPA_PLATFORM=offscreen /usr/bin/designrc \
  --project-backend-regression /workspace/tests/fixtures/Curved_LE_TE.designrc
if [[ -f '/workspace/out/Parts/Ailerons Spars.designrc' ]]; then
  runuser -u releasecheck -- env QT_QPA_PLATFORM=offscreen /usr/bin/designrc \
    --project-backend-regression '/workspace/out/Parts/Ailerons Spars.designrc'
fi
set +e
runuser -u releasecheck -- env LIBGL_ALWAYS_SOFTWARE=1 QT_QPA_PLATFORM=xcb \
  timeout 12s xvfb-run -a /usr/bin/designrc > /tmp/designrc-gui.log 2>&1
gui_status=$?
set -e
cat /tmp/designrc-gui.log
test "$gui_status" -eq 124
if grep -Ei 'could not|failed|fatal|segmentation|Aborted' /tmp/designrc-gui.log; then
  echo 'GUI startup error' >&2
  exit 1
fi
apt-get remove -y designrc
test ! -e /usr/lib/designrc/designrc
source /etc/os-release
echo "Ubuntu $VERSION_ID: $version install, libraries, generation, GUI startup, and uninstall PASSED"
