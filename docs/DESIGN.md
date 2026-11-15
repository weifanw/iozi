# Design

## Data flow and representation

The corpus loader discovers eligible files, sorts their relative paths and reads their contents under explicit limits. Each file becomes a document with a stable numeric ID, its relative path as the key, its text and a token count.

The engine stores an inverted index. Each normalized term maps to postings containing a document ID and the term's frequency in that document. Search visits the postings of the distinct query terms, accumulates scores for candidate documents and applies the requested match mode. Results are ordered by descending score, then by ascending document key.

The document text is retained for result snippets. Search does not reopen corpus files. Changes to files after startup are visible only after restarting the program. There is no index serialization, background refresh or document-update API.

## Tokenization and query semantics

The default tokenizer recognizes `[A-Za-z0-9]+`, converts ASCII uppercase letters to lowercase and treats every other byte as a delimiter. Its output does not depend on the process locale. The default minimum token length is one; a different `AsciiTokenizer<MinTokenLength>` can be selected when using the library.

The same tokenizer processes document text and query text. Query terms are deduplicated before scoring. In `any` mode, at least one term must occur in a document. In `all` mode, every distinct query term must occur, including terms absent from the index. An empty query produces no results in either mode.

Consequences worth checking when choosing a corpus:

- `Engine`, `ENGINE` and `engine` match the same keyword.
- `search-engine` becomes two keywords, not a phrase.
- `C++` and `C` both become `c`.
- `café` contributes `caf`; the non-ASCII bytes are delimiters.
- Chinese text contributes no Chinese keywords. UTF-8 file acceptance is not Unicode search support.

There is no stop-word list, stemming, phrase matching, fuzzy matching, field weighting or semantic retrieval. Filenames identify results; only document contents are indexed.

## BM25 ranking

For each distinct query term occurring in a document, the engine adds:

```text
idf(t) = ln(1 + (N - df(t) + 0.5) / (df(t) + 0.5))

term_score(t, d) = idf(t) * tf(t, d) * (k1 + 1)
                  / (tf(t, d) + k1 * (1 - b + b * length(d) / avg_length))

score(q, d) = sum(term_score(t, d)) over distinct matched query terms
```

| Symbol | Meaning |
| --- | --- |
| `N` | Number of indexed documents, including empty documents. |
| `df(t)` | Number of documents containing term `t`. |
| `tf(t, d)` | Number of occurrences of `t` in document `d`. |
| `length(d)` | Token count of document `d`. |
| `avg_length` | Total token count divided by `N`. |
| `k1` | Term-frequency saturation parameter; default `1.2`. |
| `b` | Document-length normalization parameter; default `0.75`. |

Rarer terms contribute more, repeated occurrences have diminishing returns, and longer documents receive a normalization adjustment. An empty corpus or a corpus with no tokens has no matching postings and produces no scores. Empty documents count toward corpus statistics but cannot match a keyword.

Scores are ranking values, not probabilities of relevance. The default parameters are not tuned on a relevance-labelled benchmark. A numerically correct BM25 implementation does not establish that it is optimal for a particular collection. Exact floating-point values can differ slightly across toolchains; deterministic tie-breaking applies to equal computed scores.

## Template metaprogramming

`BasicSearchEngine<Tokenizer, Ranker>` accepts tokenizer and ranker policies validated with C++20 concepts. This allows the core algorithm to be reused without virtual dispatch while keeping invalid policy types out of normal instantiations.

`AsciiTokenizer<MinTokenLength>` generates a 256-entry character table at compile time using `std::index_sequence`. The minimum token length is also a compile-time setting. The table makes byte classification and ASCII case conversion explicit rather than relying on locale-sensitive character functions.

`BM25<K1Percent, BPercent>` expresses scoring parameters as integer template arguments. Defaults `120` and `75` represent `1.2` and `0.75`; compile-time checks reject unsupported configurations. This is a deliberate API constraint for a compact example, rather than a claim that ranking parameters inherently need compile-time configuration.

A tokenizer policy must provide a const call operator taking `std::string_view` and returning `std::vector<std::string>`. It must use consistent document and query tokenization and must not emit empty terms. A ranker policy has the following call signature, with a return value convertible to `double`:

```cpp
double operator()(std::size_t term_frequency,
                  std::size_t document_length,
                  double average_document_length,
                  std::size_t document_count,
                  std::size_t document_frequency) const;
```

Concepts validate the operations and types, not the mathematical quality of a custom policy. The engine rejects non-finite score contributions and non-finite combined scores. Document keys must be nonempty and unique. Document IDs remain stable, but adding a document can invalidate references returned by `document()`. The engine does not support concurrent mutation.

## Cost and operating limits

Tokenization is linear in input bytes. Each document's term frequencies use an ordered map, requiring logarithmic lookups in that document's vocabulary. The global term dictionary and document-key dictionary use hash maps with average constant-time lookup, excluding string hashing and comparison costs. Path discovery also sorts the file list. Retained data includes document text, vocabulary strings and one posting for each distinct term/document pair, with container overhead; tokenization and per-document counting add temporary allocations.

A query sorts and deduplicates its terms, examines relevant postings, accumulates candidate scores and sorts `C` candidate documents in `O(C log C)`. Hash-table operations have their usual average-case rather than worst-case constant-time behaviour. Returning a small `--top` value does not eliminate the cost of sorting all candidates. `all` mode is a membership filter over candidates, not a dedicated optimized postings-intersection planner.

The loader defaults to 1 MiB per file, 64 MiB of text and 10,000 documents. These limits restrict input volume, not peak process memory. Many distinct small tokens can consume substantially more memory than their input bytes. Queries are limited to 4,096 bytes, and the CLI accepts result limits from 1 to 1,000. The library's own `limit = 0` means return no results.

## File handling and error boundaries

Only regular `.txt` and `.md` files are eligible; extension checks are case-insensitive. Symlinks and hidden files or directories are skipped. A selected file with a NUL byte is rejected as unsupported binary input. Reads are bounded rather than relying only on an earlier file-size check. Loader failures are reported instead of returning an apparently complete partial corpus.

Paths, snippets and diagnostic text escape control and non-ASCII bytes as `\xHH`; snippets also collapse ordinary whitespace. This prevents raw terminal control bytes from being emitted, at the cost of natural Unicode display. This is a local, single-process demonstration intended for ordinary text collections. Standard-library filesystem discovery is not a sandbox against a hostile process concurrently replacing files or changing the directory tree. Corpus limits do not constitute denial-of-service hardening for a public service.

## Verification strategy

Engine unit tests verify normalization, independently calculated BM25 scores, match modes, unknown terms, duplicate queries, deterministic ties, limits and policy customization. Loader tests exercise discovery, ordering, bounds and failure paths. CLI integration tests validate argument errors, one-shot queries, interactive commands and exit codes. Sanitizer builds provide additional checks for memory and undefined-behaviour defects on supported platforms.

No web crawler, HTTP server, GUI, persistent index or multilingual language model is required to run this version. These remain outside its declared scope.
