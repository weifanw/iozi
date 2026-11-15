# Browser application

The root page is the search application itself: a collection panel, query input,
ranked results and document reader. No account or application backend is needed.

## Architecture

The application uses the same `include/iozi/engine.hpp` as the native CLI.
`web/bridge.cpp` exposes it through Emscripten/Embind. The browser loads
`engine.mjs` and `engine.wasm`; `web/search-worker.js` indexes and searches in a
Web Worker so that the interface remains responsive.

`web/app.js` reads selected files into memory, passes their text to the worker and
renders results with text nodes. Document content and search queries are not sent
to an API or stored in browser storage. The initial requests download the app,
the Wasm module and the bundled example collection. User-selected files remain
in the current session and are cleared on reload.

The browser supports `.txt` and `.md` files. It limits a collection to 100 files,
1 MiB per file and 10 MiB of text in total. Queries are limited to 4,096 UTF-8
bytes. These input limits do not describe total memory usage. NUL-containing
files and duplicate document names are rejected. A failed collection replacement
keeps the previous index available.

The search tokenizer is still ASCII-only, exactly as documented for the native
engine. Unicode text can be displayed, but Chinese segmentation, accent folding
and multilingual search have not been added. Ranking remains BM25. The browser
does not search the public web.

## Build

Install the official Emscripten SDK and activate it, then run:

```sh
source /path/to/emsdk/emsdk_env.sh
bash scripts/build-web.sh
npm run test:web
```

This version was built with Emscripten 7.0.0. The CI workflow pins both the SDK
manager commit and compiler version so that a future default SDK change does
not silently change the browser build.

Node.js is used only by the automated Wasm/worker tests; the browser application
has no npm runtime dependencies. The native C++ build and `make test` continue to
work without Emscripten or Node.

The build produces a static `dist/` directory. Host that directory using HTTPS.
The WebAssembly file must be served as `application/wasm` and the JavaScript
module and worker as JavaScript. Opening `index.html` through `file://` is not a
supported way to run module workers.

## Verification

`tests/test_web_engine.mjs` instantiates the actual Wasm module. The worker test
checks numerical BM25 scores, limits, failed-load rollback and ordered requests
against that same module. Browser UI and responsive behavior require an actual
browser check in addition to these tests.

## Deployment

The project is iozi. Source and build instructions currently remain at the public
`weifanw/minse` repository URL until an administrator can rename it.
The deployed Site has a separate source remote and a tracked hosting manifest.
Custom-domain DNS targets and ownership-validation records must be obtained from
the host for the particular Site; do not guess an IP address or CNAME target.
The domain is not ready until host validation, DNS resolution and HTTPS all pass.

The current custom-domain target is `iozi.io`. See [domain setup](DOMAIN.md).
