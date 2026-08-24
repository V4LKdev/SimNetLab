#!/bin/sh

set -u

bundle_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$bundle_root" || exit 1

if [ "$(uname -m)" != "x86_64" ]; then
    echo "SimNetLab Demo supports x86_64 Linux only." >&2
    exit 1
fi

if [ -z "${DISPLAY:-}" ]; then
    echo "SimNetLab Demo needs a graphical Linux session with X11 or XWayland." >&2
    exit 1
fi

if [ ! -x bin/Server ] || [ ! -x bin/Client ]; then
    echo "The demo executables are missing. Re-extract the complete archive." >&2
    exit 1
fi

mkdir -p logs || {
    echo "Cannot create the writable logs directory: $bundle_root/logs" >&2
    exit 1
}

run_id="demo-$(date +%Y%m%d-%H%M%S)-$$"
server_console="logs/${run_id}-server-console.log"
server_pid=

cleanup()
{
    saved_status=$?
    trap - EXIT HUP INT TERM
    if [ -n "$server_pid" ] && kill -0 "$server_pid" 2>/dev/null; then
        kill "$server_pid" 2>/dev/null || true
    fi
    if [ -n "$server_pid" ]; then
        wait "$server_pid" 2>/dev/null || true
    fi
    exit "$saved_status"
}

on_signal()
{
    exit 130
}

trap cleanup EXIT
trap on_signal HUP INT TERM

echo "Starting SimNetLab Demo..."
echo "Server log: $bundle_root/$server_console"

bin/Server \
    --config config/server_demo.json \
    --shared-config config/shared_demo_visual.json \
    --run-id "$run_id" \
    >"$server_console" 2>&1 &
server_pid=$!

sleep 1
if ! kill -0 "$server_pid" 2>/dev/null; then
    wait "$server_pid"
    server_status=$?
    echo "The demo Server failed to start (status $server_status)." >&2
    echo "See $bundle_root/$server_console" >&2
    exit "$server_status"
fi

bin/Client \
    --config config/client_demo.json \
    --shared-config config/shared_demo_visual.json \
    --run-id "$run_id"
client_status=$?

exit "$client_status"
