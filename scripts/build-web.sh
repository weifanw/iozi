#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"
compiler="${EMXX:-em++}"
if ! command -v "$compiler" >/dev/null 2>&1; then
    printf '%s\n' 'Emscripten em++ is required. Activate emsdk or set EMXX to its absolute path.' >&2
    exit 1
fi
mkdir -p dist
"$compiler" web/bridge.cpp -Iinclude -std=c++20 -O3 -Wall -Wextra -Wpedantic \
    -lembind -fexceptions -sDISABLE_EXCEPTION_CATCHING=0 \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createIoziEngine \
    -sENVIRONMENT=web,worker,node -sALLOW_MEMORY_GROWTH=1 \
    -sMAXIMUM_MEMORY=268435456 -sFILESYSTEM=0 -sDYNAMIC_EXECUTION=0 \
    -o dist/engine.mjs
# Keep the binary filename stable while exposing a JS lifetime/error adapter.
mv dist/engine.mjs dist/engine-core.mjs
for asset in engine.mjs index.html app.js style.css search-worker.js favicon.svg samples.json robots.txt sitemap.xml; do
    if [[ -f "web/$asset" ]]; then
        cp "web/$asset" "dist/$asset"
    fi
done
printf '%s\n' 'Built dist/engine.mjs and dist/engine.wasm from the C++ core.'
