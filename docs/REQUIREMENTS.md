# Requirements and provenance

## Original repository evidence

The starting point was commit [`590dbd0be49b9045ca82fbf7a2791231c21d8f70`](https://github.com/weifanw/minse/commit/590dbd0be49b9045ca82fbf7a2791231c21d8f70), dated 20 April 2023. Its complete tracked file set was:

| File | Observed contents |
| --- | --- |
| `README.md` | `# minse`, followed by `Mini Search Engine`. |
| `LICENSE` | MIT License, copyright 2023 Weifan Wei. |
| `.gitignore` | A Python-oriented ignore template. |

There was no search implementation, build configuration, test suite or technical specification. The repository name and description establish a project idea, not an existing search capability or a commitment to a particular architecture. The original MIT licence is retained; the README and ignore rules are replaced for the implementation.

## Explicit requirements for this implementation

The request of 15 November 2026 asks for:

- A minimal search engine that can be run, tested and demonstrated.
- C++ as the default implementation language, using the standard library where practical and including template metaprogramming.
- Text indexing, keyword search, simple relevance ranking and command-line interaction.
- Complete source code, example data, unit tests and running instructions.
- Execution of tests where the environment permits, with discovered problems fixed.
- Reviewable code and files before directly updating the GitHub repository.
- A clear distinction between original requirements and newly chosen design. Existing repository content need not be preserved.

## Added design decisions

These are implementation choices made for the current version, not requirements recovered from the original README.

| Decision | Reason and boundary |
| --- | --- |
| C++20 and standard-library-only engine | Supports concepts, filesystem operations and compile-time tables without runtime dependencies. |
| Local `.txt` and `.md` corpus | Makes a useful demonstration possible without a crawler, server or external service. |
| In-memory inverted index | Avoids rereading every document for each interactive query. It is rebuilt on startup. |
| ASCII letters and digits, lowercase normalization | Provides deterministic, locale-independent tokenization. Unicode segmentation is explicitly outside this version. |
| BM25 with `k1 = 1.2`, `b = 0.75` | Supplies term-frequency saturation and document-length normalization with an inspectable formula. Parameters are defaults, not empirically tuned values. |
| `any` and `all` match modes | Makes multi-keyword behaviour explicit. No phrase, Boolean-expression or proximity parser. |
| Query term deduplication and lexical tie-breaking | Prevents accidental repeated-word boosting and makes output repeatable. |
| Compile-time tokenizer and scorer policies | Gives template metaprogramming a bounded role without making the CLI generic for its own sake. |
| File, corpus, document-count and query limits | Bounds accepted input; it is not a guarantee of a particular peak memory usage. |
| C++ unit tests and Python standard-library CLI tests | Tests the library and the program boundary without a test-framework dependency. Python is only a development dependency. |
| Make and CMake build entry points | Supports a short local workflow and a conventional portable build description. |

## Acceptance checks

| Requested outcome | Review or verification entry point |
| --- | --- |
| Text indexing and keyword retrieval | `include/iozi/engine.hpp`, engine unit tests. |
| Relevance ranking | BM25 implementation, numerical score checks and ordering tests. |
| Command-line interaction | `src/main.cpp`, `--help`, one-shot queries, REPL integration tests. |
| Text-file ingestion | `src/corpus.cpp`, corpus unit tests and loader error cases. |
| Template metaprogramming | Tokenizer lookup-table generation, policy concepts, compile-time scorer configuration. |
| Runnable example | `examples/documents/`, `make demo`. |
| Reproducible testing | `make test`, `make sanitize`, CMake/CTest entry points. |
| Clear operation and limitations | `README.md`, `docs/DESIGN.md`. |

Validation results must come from the actual build and test run for the delivered commit. A source file, build target or workflow definition alone is not evidence that a test passed.

## Browser experience extension

The user subsequently requested the complete experience at a custom domain.
This adds a public browser interface to the existing C++ project. Using WebAssembly, a Web Worker, a local file picker,
session-only document state and the browser-specific input limits are new
implementation decisions. The native engine, ranking and ASCII scope are
retained. The website does not crawl or search the public web.

## Current name and domain

On 15 November 2026 the user corrected the earlier domain information: the
original proposed domain had expired and was no longer available to them. They
explicitly chose **iozi**, with `iozi.io` as the replacement domain, and requested
the project be renamed throughout. The executable, C++ namespace and include
paths, build targets, WebAssembly factory, website branding and operating
instructions now use iozi. The initial README quotation above remains verbatim
as historical evidence. Existing Git history and the original license are
preserved. Custom-domain readiness requires separate DNS and HTTPS verification.
