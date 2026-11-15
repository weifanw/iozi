# iozi

A small, runnable search engine for local text files, written in C++20 with the standard library. It builds an inverted index, finds keyword matches and ranks them with BM25. Search once from the command line or keep the index open in an interactive session.

**Scope:** ASCII keyword search over `.txt` and `.md` files. No external runtime libraries, web service or database. The index is rebuilt in memory on startup.

## Use in your browser

[Open the browser app](https://minse.weifan-wei.chatgpt.site/).
The product is now **iozi**. The selected custom domain is `iozi.io`; its DNS and
HTTPS activation are pending. The working hosted URL above retains the original
platform slug. See [domain setup](docs/DOMAIN.md) for the actual DNS records.
The homepage is the full search workspace: search the sample collection or
import local `.txt` and `.md` files, inspect ranked results and read documents.
Files and queries stay in the browser tab; reloading clears imported files.

The browser runs the same C++ engine compiled to WebAssembly. It supports
100 documents, 1 MiB per file and 10 MiB per collection. The ASCII keyword
scope below applies to both interfaces. See [browser build and architecture](docs/WEB.md).

The GitHub repository URL still uses its original name pending an administrator
rename. The project, executable and C++ API are all named `iozi`.

## Run the command-line tool

Requirements: a C++20 compiler and GNU Make. The test suite also uses Python 3's standard library; the engine itself does not use Python.

```sh
git clone https://github.com/weifanw/minse.git iozi
cd iozi
make
./build/iozi --data examples/documents --query "search engine" --top 3
```

The sample query ranks these documents first (snippets omitted here):

```text
Indexed 6 documents (233 terms, 394 tokens).
1. 01-search-engine.txt  [score=2.597244]
2. 03-cpp-templates.txt  [score=1.441518]
3. 04-ranking.txt  [score=0.604070]
```

Try these queries against the six included documents:

```sh
# Any keyword can match; results are ranked by relevance.
./build/iozi --data examples/documents --query "search index"

# Every distinct keyword must occur in a document.
./build/iozi --data examples/documents --query "search engine" --mode all

# Keep the loaded index open for successive queries.
./build/iozi --data examples/documents
```

In interactive mode, enter keywords or use `:help`, `:stats` and `:quit`. End-of-file also exits. Results show a document path, a BM25 score and a text snippet. Output escapes control and non-ASCII bytes as `\xHH`; snippets collapse ordinary whitespace.

For a build without Make or CMake:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude \
    src/main.cpp src/corpus.cpp -o iozi
./iozi --data examples/documents --query "template"
```

## Command-line interface

```text
iozi --data DIRECTORY [--query TEXT] [--mode any|all] [--top N]
iozi --help
```

| Option | Behaviour |
| --- | --- |
| `--data DIRECTORY` | Recursively load eligible text files from this directory. |
| `--query TEXT` | Run one query and exit. Omit it for interactive mode. |
| `--mode any` | Match at least one query keyword; the default. |
| `--mode all` | Require every distinct query keyword. |
| `--top N` | Return at most `N` matches; default 10, accepted range 1–1000. |
| `--help` | Print usage without loading files; use this option alone. |

Quote queries containing spaces. Search is case-insensitive for ASCII letters. Punctuation separates tokens, so `C++` becomes `c`, `full-text` becomes `full` and `text`, and `don't` becomes `don` and `t`. Only ASCII letters and digits form keywords. Repeated query keywords count once. Empty or punctuation-only queries return no matches; an unknown keyword prevents a match in `all` mode.

Files are loaded in sorted relative-path order. Extensions are case-insensitive; symlinks, hidden files and hidden directories are skipped. Empty files are valid documents. The default limits are 1 MiB per file, 64 MiB of file contents in total, 10,000 documents and 4,096 bytes per query. The byte limits do not bound total process memory: the index and token strings also consume memory. Missing directories, no eligible files, read errors, NUL-containing files and exceeded corpus limits produce an error.

Exit codes: `0` for a completed search, including no matches; `1` for corpus or input errors; `2` for invalid command-line usage. An overlong `--query` is a usage error; an overlong interactive query terminates the session with an input error.

## Build and test

```sh
make test       # C++ unit tests and Python CLI integration tests
make demo       # Run a sample query
make sanitize   # AddressSanitizer + UndefinedBehaviorSanitizer tests
```

Sanitizer support depends on the compiler and platform. Alternatively, use CMake 3.20 or later:

```sh
cmake -S . -B build-cmake
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
./build-cmake/iozi --data examples/documents --query "search engine"
```

Set `-DBUILD_TESTING=OFF` when configuring CMake if only the executable is needed; that build does not require Python. Multi-configuration generators may place the executable in a configuration subdirectory such as `Debug/`.

The unit tests cover token normalization, ranking, match modes, deterministic ordering, corpus loading and error cases. Integration tests exercise the executable. These commands describe how to verify a checkout; they are not a claim that every platform has been tested.

See the [validation record](docs/VALIDATION.md) for actual local results and the
environment's LeakSanitizer limitation.

The [GitHub Actions workflow](.github/workflows/ci.yml) is configured to build and test on Ubuntu with GCC and Clang, with AddressSanitizer and UndefinedBehaviorSanitizer enabled. Check the [Actions page](https://github.com/weifanw/minse/actions) for actual run results.

## How it works

1. Load and tokenize each document once.
2. Build an inverted index from each term to its document IDs and term frequencies.
3. Retrieve postings for the normalized query terms.
4. Sum BM25 contributions and sort by descending score, breaking equal scores by document path.

`BasicSearchEngine<Tokenizer, Ranker>` uses C++20 concepts to validate interchangeable policies at compile time. `AsciiTokenizer<MinTokenLength>` builds its character lookup table with an index sequence; `BM25<K1Percent, BPercent>` validates its scoring parameters at compile time. This keeps template metaprogramming tied to configurable behaviour and compile-time checks.

The core is a header-only library and can also be used directly:

```cpp
#include "iozi/engine.hpp"

iozi::BasicSearchEngine<iozi::AsciiTokenizer<2>, iozi::BM25<150, 75>> engine;
engine.add_document("notes.txt", "A small search engine with a local index.");
auto hits = engine.search("search index", {iozi::MatchMode::all, 5});
// hits contain document IDs and scores; engine.document(id) returns the document.
```

See [design and scoring](docs/DESIGN.md) for the formula, data structures and limitations, and [requirements and provenance](docs/REQUIREMENTS.md) for the distinction between the original repository, the current request and implementation choices.

## 中文说明

这是一个可以本地运行的 C++20 最小搜索引擎，包含倒排索引、关键词检索、BM25 排序、命令行交互、示例数据和测试。运行 `make` 后，执行：

```sh
./build/iozi --data examples/documents --query "search engine"
```

本版以英文 ASCII 关键词为范围，**不支持中文分词或 Unicode 大小写归一化**。可以读取含 UTF-8 内容的文件，但非 ASCII 字节只作为分隔符，不会被索引为中文关键词。索引不写入磁盘，每次启动重新构建。原仓库只有项目名称和简短说明；此处的技术架构是本次新增设计，并非原始需求中已有的实现。

MIT licensed; see [LICENSE](LICENSE).
