#!/bin/bash
# SessionStart hook for Claude Code on the web: toolchain + a configured and built tree in build/, so
# `ctest --test-dir build`, single test binaries (build/bin/anav_*_tests <filter>) and clang-format work right away.
# Idempotent: later sessions only rebuild what changed (the container state is cached after this hook).
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
    exit 0
fi

cd "$CLAUDE_PROJECT_DIR"

# Toolchain, usually preinstalled in the web image.
need=()
command -v cmake >/dev/null || need+=(cmake)
command -v ninja >/dev/null || need+=(ninja-build)
command -v g++ >/dev/null || need+=(g++)
command -v clang-format >/dev/null || need+=(clang-format)

if [ ${#need[@]} -gt 0 ]; then
    SUDO=""
    [ "$(id -u)" -ne 0 ] && SUDO="sudo"
    $SUDO apt-get update -qq
    $SUDO apt-get install -y -qq --no-install-recommends "${need[@]}"
fi

# Configure once. LTO is off for fast incremental builds. The exporter needs StormLib from GitHub: without network
# access everything else (core, server, unit/e2e tests) is still built.
if [ ! -f build/CMakeCache.txt ]; then
    flags=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DANAV_ENABLE_LTO=OFF -DANAV_WARNINGS_AS_ERRORS=ON)

    if ! cmake -S . -B build "${flags[@]}"; then
        echo "session-start: configure failed (StormLib not reachable?), building without the exporter" >&2
        rm -rf build/CMakeCache.txt build/CMakeFiles build/_deps
        cmake -S . -B build "${flags[@]}" -DANAV_BUILD_EXPORTER=OFF
    fi
fi

cmake --build build

# NuGet packages for the C# client and its integration test (best effort).
if command -v dotnet >/dev/null; then
    dotnet restore AmeisenNavigation.Client/AmeisenNavigation.Client.csproj >/dev/null || true
    dotnet restore tests/ClientIntegration/ClientIntegration.csproj >/dev/null || true
fi
