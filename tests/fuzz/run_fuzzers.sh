#!/usr/bin/env bash
# Run every fuzzer of a -DANAV_BUILD_FUZZERS=ON build.
#   tests/fuzz/run_fuzzers.sh <build dir> [seconds per fuzzer, default 60]
# Replays tests/fuzz/regressions/<target>/* first (inputs that crashed before), then fuzzes from the seed corpus.
# Crashes land in <build dir>/artifacts.
set -euo pipefail

BUILD="$(realpath "$1")"
SECONDS_PER_TARGET="${2:-60}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CORPUS="$BUILD/corpus"
ARTIFACTS="$BUILD/artifacts"
mkdir -p "$CORPUS" "$ARTIFACTS"

if [ -x "$BUILD/bin/FuzzCorpus" ]; then
    "$BUILD/bin/FuzzCorpus" "$CORPUS"
fi

status=0

for target in Tile Anp Exporter Protocol; do
    bin="$BUILD/bin/Fuzz$target"
    name="$(echo "$target" | tr '[:upper:]' '[:lower:]')"
    [ -x "$bin" ] || continue

    regressions="$ROOT/tests/fuzz/regressions/$name"

    if [ -d "$regressions" ] && [ -n "$(ls -A "$regressions")" ]; then
        echo "== $target: replaying regressions"
        "$bin" "$regressions"/* || status=1
    fi

    echo "== $target: fuzzing for ${SECONDS_PER_TARGET}s"
    mkdir -p "$CORPUS/$name"

    if ! "$bin" "$CORPUS/$name" -max_total_time="$SECONDS_PER_TARGET" -rss_limit_mb=4096 -max_len=65536 \
        -timeout=30 -artifact_prefix="$ARTIFACTS/$name-" -print_final_stats=1; then
        status=1
    fi
done

exit $status
