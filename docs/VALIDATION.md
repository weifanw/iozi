# Validation record

Validated on 15 November 2026 in Linux with GCC 14.2.0, CMake 4.4.5 and
Python 3.12.14. These results apply to the source accompanying this record.
The starting repository commit was `590dbd0be49b9045ca82fbf7a2791231c21d8f70`.

| Check | Observed result |
| --- | --- |
| `make test` | 9 engine test groups, 8 corpus test groups and 14 CLI tests passed. |
| CMake Release build and CTest | All 3 suites passed, including the same CLI tests. |
| `ASAN_OPTIONS=detect_leaks=0 make sanitize` | All tests passed with AddressSanitizer and UndefinedBehaviorSanitizer. |
| `make demo` | Indexed 6 documents, 233 distinct terms and 394 tokens; ranked results match the README. |
| Independent source review and boundary probes | No unresolved correctness blocker identified after the fixes below. |
| Warning flags and whitespace check | No compiler warnings with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`; `git diff --check` passed. |

The first unmodified `make sanitize` attempt could not finish because
LeakSanitizer reported that it cannot operate under this environment's ptrace
instrumentation. Only leak detection was disabled for the successful local
sanitizer run. **Local memory-leak checking is therefore unverified.** The
repository's CI configuration keeps the normal sanitizer settings.

Review found and fixed three defects, with regression coverage:

1. An exactly 4,096-byte interactive query was accepted with LF but rejected
   with CRLF. Both line endings are now accepted at the same content limit.
2. A symlink supplied as the corpus root could bypass its rejection when
   followed by `/` or `/.`. These final-component spellings are now checked.
   Intermediate path components can still be symlinks; this loader is not a
   filesystem sandbox and assumes a stable local directory.
3. Repeated query terms multiplied the work of finding a result snippet.
   Snippet membership now uses one deduplicated term set shared by all results.
   An independent probe using a 1,000,001-byte document and a 4,094-byte
   repeated query produced identical output to a single-term query and took
   approximately 0.019 seconds after the fix versus approximately 13.5 seconds
   before it. These are indicative local measurements, not a portable benchmark.

The GitHub Actions workflow defines GCC and Clang checks on Ubuntu. Its actual
run status must be read from [GitHub Actions](https://github.com/weifanw/minse/actions).
This pre-publication record does not claim a remote workflow passed. Windows,
macOS and multilingual retrieval were not validated locally.

## Browser extension validation

The browser extension was built with Emscripten 7.0.0. Eight Wasm test groups
and a worker suite pass against the actual compiled C++ module. Tests cover
independent BM25 values, match modes, limits, invalid data, rollback, ordered
requests and hundreds of rejected large arguments followed by successful use.

An initial bridge leaked converted strings when throwing JavaScript exceptions
from C++. It now returns an error envelope normally, and a JavaScript adapter
throws after cleanup. Review also fixed collection-loading races, import-error
recovery and a Unicode/ASCII normalization mismatch in the UI.

Chromium browser checks verified the default sample search, all-keyword mode,
local file import, document viewing with literal HTML and Chinese text, rejected
binary input preserving the existing collection, removal and reload clearing
user files. A 390-pixel iframe viewport verified the responsive layout without
horizontal overflow, search shortcuts and access to the collapsed collection.
This is not a claim of testing a physical mobile device or every browser.

The native C++ engine was not modified for this extension.

## iozi rename validation

On 15 November 2026, after renaming the executable, namespace, include directory,
build targets and WebAssembly factory, the following checks were rerun:

- `make BUILD_DIR=build-iozi test`: 9 engine groups, 8 corpus groups and 14 CLI
  tests passed.
- A fresh CMake Release build in `build-cmake-iozi`: all 3 CTest suites passed.
- Emscripten 7.0.0 rebuilt the actual WebAssembly module; all 8 Wasm groups and
  the worker suite passed.
- The sample CLI query and the `iozi>` interactive prompt were verified.
- Chromium rendered the iozi wordmark and title, returned the expected 4 sample
  matches and 1 template match, and opened the full document reader.

The rename does not change tokenization or ranking. Sanitizer results above
belong to the earlier implementation validation; sanitizers were not rerun for
this naming-only change. The earlier GitHub Actions jobs could not start because
the account reported a billing lock; local results do not imply remote CI passed.
