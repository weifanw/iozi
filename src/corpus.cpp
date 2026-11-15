#include "iozi/corpus.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace iozi {
namespace {

bool supported_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    for (auto& c : extension) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return extension == ".txt" || extension == ".md";
}

std::string read_text(const std::filesystem::path& path,
                      std::size_t file_limit, std::size_t total_remaining) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path.string());
    }

    std::string text;
    std::array<char, 8192> buffer{};
    for (;;) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(file.gcount());
        if (file.bad() || (file.fail() && !file.eof())) {
            throw std::runtime_error("Cannot read file: " + path.string());
        }
        if (count > file_limit - text.size()) {
            throw std::runtime_error("File byte limit exceeded: " + path.string());
        }
        if (count > total_remaining - text.size()) {
            throw std::runtime_error("Total corpus byte limit exceeded: " + path.string());
        }
        if (std::find(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count), '\0')
            != buffer.begin() + static_cast<std::ptrdiff_t>(count)) {
            throw std::runtime_error("NUL byte in text file: " + path.string());
        }
        text.append(buffer.data(), count);
        if (file.eof()) {
            break;
        }
    }
    return text;
}

} // namespace

std::vector<CorpusDocument> load_corpus(const std::filesystem::path& directory,
                                       LoaderLimits limits) {
    if (limits.max_file_bytes == 0 || limits.max_total_bytes == 0
        || limits.max_documents == 0) {
        throw std::invalid_argument("All corpus limits must be positive");
    }
    // Remove only trailing separators and '.' components for the symlink
    // check: symlink_status("link/") can otherwise follow the final link.
    // Do not normalize '..': that could change the OS meaning of 'link/..'.
    auto root_probe = directory;
    while (root_probe.has_relative_path()
           && (root_probe.filename().empty() || root_probe.filename() == ".")) {
        auto parent = root_probe.parent_path();
        if (parent.empty()) {
            root_probe = ".";
            break;
        }
        root_probe = std::move(parent);
    }
    const auto status = std::filesystem::symlink_status(root_probe);
    if (std::filesystem::is_symlink(status)
        || !std::filesystem::is_directory(status)) {
        throw std::runtime_error("Data path must be a non-symlink directory: "
                                 + directory.string());
    }

    std::vector<std::pair<std::string, std::filesystem::path>> paths;
    for (std::filesystem::recursive_directory_iterator it(directory), end; it != end; ++it) {
        const auto& entry = *it;
        const auto filename = entry.path().filename().string();
        const auto entry_status = entry.symlink_status();
        if (!filename.empty() && filename.front() == '.') {
            it.disable_recursion_pending();
            continue;
        }
        if (std::filesystem::is_symlink(entry_status)) {
            it.disable_recursion_pending();
            continue;
        }
        if (!std::filesystem::is_regular_file(entry_status)
            || !supported_extension(entry.path())) {
            continue;
        }
        if (paths.size() == limits.max_documents) {
            throw std::runtime_error("Corpus document limit exceeded");
        }
        const auto relative = entry.path().lexically_relative(directory).generic_string();
        paths.emplace_back(relative, entry.path());
    }
    if (paths.empty()) {
        throw std::runtime_error("No eligible .txt or .md files in: " + directory.string());
    }
    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    std::vector<CorpusDocument> documents;
    documents.reserve(paths.size());
    std::size_t total_bytes = 0;
    for (const auto& [key, path] : paths) {
        auto text = read_text(path, limits.max_file_bytes, limits.max_total_bytes - total_bytes);
        total_bytes += text.size();
        documents.push_back({key, std::move(text)});
    }
    return documents;
}

} // namespace iozi
