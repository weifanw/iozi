#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace iozi {

struct LoaderLimits {
    std::size_t max_file_bytes = 1024 * 1024;
    std::size_t max_total_bytes = 64 * 1024 * 1024;
    std::size_t max_documents = 10000;
};

struct CorpusDocument {
    std::string key;
    std::string text;
};

// Load regular .txt/.md files recursively, sorted by relative path. Hidden
// entries and symlinks are excluded. The final root component cannot be a
// symlink; intermediate components may be symlinks. Errors fail the entire
// load; no partial corpus is returned. Intended for a local directory that is
// not being mutated concurrently (std::filesystem cannot prevent link races).
[[nodiscard]] std::vector<CorpusDocument> load_corpus(
    const std::filesystem::path& directory, LoaderLimits limits = {});

} // namespace iozi
