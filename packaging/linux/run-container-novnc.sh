#!/bin/sh
set -eu

display_number=1
display=":$display_number"
vnc_port=5901
web_port=6080

cleanup() {
  for process_id in ${designrc_pid-} ${openbox_pid-} ${xvnc_pid-}; do
    if [ -n "$process_id" ]; then
      kill "$process_id" 2>/dev/null || true
    fi
  done
}
trap cleanup EXIT HUP INT TERM

rm -f "/tmp/.X${display_number}-lock" \
  "/tmp/.X11-unix/X${display_number}"

Xvnc "$display" \
  -geometry 1600x1000 \
  -depth 24 \
  -SecurityTypes None \
  -rfbport "$vnc_port" \
  -localhost &
xvnc_pid=$!

attempt=0
while ! DISPLAY="$display" xdpyinfo >/dev/null 2>&1; do
  attempt=$((attempt + 1))
  if [ "$attempt" -ge 50 ]; then
    echo "The virtual display did not start." >&2
    exit 1
  fi
  sleep 0.1
done

DISPLAY="$display" openbox &
openbox_pid=$!
DISPLAY="$display" /usr/bin/designrc &
designrc_pid=$!

exec websockify \
  --web /usr/share/novnc \
  "0.0.0.0:$web_port" \
  "localhost:$vnc_port"
