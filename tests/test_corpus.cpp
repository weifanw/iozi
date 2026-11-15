#include "iozi/corpus.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template<class Function>
void expect_error(Function function, std::string_view contains = {}) {
    try {
        function();
    } catch (const std::exception& error) {
        require(std::string_view(error.what()).find(contains) != std::string_view::npos,
                "Exception did not contain expected explanation");
        return;
    }
    throw std::runtime_error("Expected an exception");
}

struct TemporaryDirectory {
    fs::path path;

    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 1000; ++attempt) {
            const auto candidate = fs::temp_directory_path()
                / ("iozi-corpus-test-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (fs::create_directory(candidate)) {
                path = candidate;
                return;
            }
        }
        throw std::runtime_error("Could not create unique test directory");
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    void write(const fs::path& relative, std::string_view text) const {
        const auto destination = path / relative;
        fs::create_directories(destination.parent_path());
        std::ofstream stream(destination, std::ios::binary);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.close();
        require(static_cast<bool>(stream), "Could not write test fixture");
    }
};

void recursive_order_and_filters() {
    TemporaryDirectory directory;
    directory.write("z.TXT", "last");
    directory.write("notes/A.Md", "middle\r\n");
    directory.write("a.txt", "first");
    directory.write("ignore.csv", "not text");
    directory.write(".hidden.txt", "hidden");
    directory.write(".private/nested.txt", "hidden tree");
    directory.write("notes/.hidden.md", "hidden nested file");
    const auto documents = iozi::load_corpus(directory.path);
    require(documents.size() == 3, "Expected only eligible visible files");
    require(documents[0].key == "a.txt" && documents[0].text == "first", "First sorted document");
    require(documents[1].key == "notes/A.Md" && documents[1].text == "middle\r\n", "Nested raw text");
    require(documents[2].key == "z.TXT" && documents[2].text == "last", "Uppercase extension");
}

void empty_documents_are_valid() {
    TemporaryDirectory directory;
    directory.write("empty.txt", "");
    directory.write("whitespace.md", " \t\n");
    const auto documents = iozi::load_corpus(directory.path);
    require(documents.size() == 2, "Empty text is a document");
    require(documents[0].text.empty(), "Empty document preserved");
}

void exact_byte_limits() {
    TemporaryDirectory directory;
    directory.write("a.txt", "1234");
    directory.write("b.txt", "5678");
    directory.write("c.txt", "");
    const auto documents = iozi::load_corpus(directory.path, {4, 8, 3});
    require(documents.size() == 3, "Exact limits and empty final document accepted");
    expect_error([&] { (void)iozi::load_corpus(directory.path, {3, 8, 3}); }, "File byte limit");
    expect_error([&] { (void)iozi::load_corpus(directory.path, {4, 7, 3}); }, "Total corpus byte limit");
    expect_error([&] { (void)iozi::load_corpus(directory.path, {4, 8, 2}); }, "document limit");
}

void chunked_reads() {
    TemporaryDirectory directory;
    const std::string text(16384, 'a');
    directory.write("large.md", text);
    const auto documents = iozi::load_corpus(directory.path, {16384, 16384, 1});
    require(documents[0].text == text, "Exact multi-chunk text must be complete");
    expect_error([&] { (void)iozi::load_corpus(directory.path, {16383, 16384, 1}); }, "File byte limit");
    expect_error([&] { (void)iozi::load_corpus(directory.path, {16384, 16383, 1}); }, "Total corpus byte limit");
}

void nul_rejected_in_any_chunk() {
    TemporaryDirectory directory;
    directory.write("a.txt", "valid text");
    std::string text(9000, 'a');
    text[8500] = '\0';
    directory.write("b.txt", text);
    expect_error([&] { (void)iozi::load_corpus(directory.path); }, "NUL byte");
}

void missing_wrong_and_empty_directories() {
    TemporaryDirectory directory;
    expect_error([&] { (void)iozi::load_corpus(directory.path / "missing"); });
    expect_error([&] { (void)iozi::load_corpus(directory.path); }, "No eligible");
    directory.write("file.txt", "content");
    expect_error([&] { (void)iozi::load_corpus(directory.path / "file.txt"); }, "directory");
}

void positive_limits_required() {
    TemporaryDirectory directory;
    directory.write("a.txt", "a");
    expect_error([&] { (void)iozi::load_corpus(directory.path, {0, 1, 1}); }, "positive");
    expect_error([&] { (void)iozi::load_corpus(directory.path, {1, 0, 1}); }, "positive");
    expect_error([&] { (void)iozi::load_corpus(directory.path, {1, 1, 0}); }, "positive");
}

void symlinks_are_excluded() {
    TemporaryDirectory directory;
    TemporaryDirectory outside;
    directory.write("real.txt", "inside");
    outside.write("outside.txt", "outside");
    std::error_code error;
    fs::create_symlink(outside.path / "outside.txt", directory.path / "linked.txt", error);
    if (error) {
        std::cout << "[SKIP] symlink checks unavailable: " << error.message() << '\n';
        return;
    }
    fs::create_directory_symlink(outside.path, directory.path / "linked-dir");
    fs::create_directory_symlink(directory.path, directory.path / "loop");
    fs::create_symlink(directory.path / "absent", directory.path / "broken.txt");
    const auto documents = iozi::load_corpus(directory.path);
    require(documents.size() == 1 && documents[0].key == "real.txt", "All symlinks skipped");
    expect_error([&] { (void)iozi::load_corpus(directory.path / "linked-dir"); }, "non-symlink");
    expect_error([&] { (void)iozi::load_corpus(directory.path / "linked-dir" / ""); }, "non-symlink");
    expect_error([&] { (void)iozi::load_corpus(directory.path / "linked-dir" / "."); }, "non-symlink");
    require(iozi::load_corpus(directory.path / "").size() == 1, "Ordinary trailing slash accepted");
    require(iozi::load_corpus(directory.path / ".").size() == 1, "Ordinary trailing dot accepted");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"recursive order and filters", recursive_order_and_filters},
        {"empty documents", empty_documents_are_valid},
        {"exact byte and document limits", exact_byte_limits},
        {"chunked reads", chunked_reads},
        {"NUL bytes", nul_rejected_in_any_chunk},
        {"missing, wrong and empty directories", missing_wrong_and_empty_directories},
        {"positive limits", positive_limits_required},
        {"symlink exclusion", symlinks_are_excluded},
    };
    std::size_t failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " corpus tests passed.\n";
    return failures == 0 ? 0 : 1;
}
