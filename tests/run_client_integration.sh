#!/usr/bin/env bash
# Run the C# client integration test against a real server.
#   tests/run_client_integration.sh <path to AmeisenNavigation.Server> [mesh dir]
# The mesh dir defaults to the synthetic test world written by anav_e2e_tests (<tmp>/anav_tests/world).
set -euo pipefail

SERVER="$(realpath "$1")"
MESHES="${2:-${TMPDIR:-/tmp}/anav_tests/world}"
PORT="${ANAV_TEST_PORT:-47199}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"

[ -f "$MESHES/001.anp" ] || { echo "missing $MESHES/001.anp, run anav_e2e_tests first"; exit 1; }

cat > "$WORK/config.cfg" <<CFG
bUseAnpFileFormat=1
iMaxPointPath=256
iPort=$PORT
sIp=127.0.0.1
sMmapsPath=$MESHES
CFG

"$SERVER" "$WORK/config.cfg" > "$WORK/server.log" 2>&1 &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null || true; wait $SERVER_PID 2>/dev/null || true; rm -rf "$WORK"' EXIT

# Wait for the listening socket.
for _ in $(seq 1 100); do
    if (echo > /dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then break; fi
    kill -0 $SERVER_PID 2>/dev/null || { cat "$WORK/server.log"; exit 1; }
    sleep 0.1
done

if ! dotnet run --project "$ROOT/tests/ClientIntegration/ClientIntegration.csproj" -c Release -- "$PORT"; then
    echo "--- server log ---"
    cat "$WORK/server.log"
    exit 1
fi
