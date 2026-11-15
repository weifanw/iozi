#include "iozi/corpus.hpp"
#include "iozi/engine.hpp"

#include <algorithm>
#include <charconv>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace {

constexpr std::size_t max_query_bytes = 4096;

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Options {
    std::string directory;
    std::optional<std::string> query;
    iozi::SearchOptions search;
    bool help = false;
};

// Emit ASCII only: filenames, document excerpts and OS error messages may
// contain arbitrary bytes, including terminal control sequences.
std::string printable(std::string_view input) {
    constexpr std::string_view hex = "0123456789ABCDEF";
    std::string output;
    for (const unsigned char c : input) {
        if (c >= 32 && c <= 126) {
            output.push_back(static_cast<char>(c));
        } else {
            output += "\\x";
            output += hex[c >> 4];
            output += hex[c & 15];
        }
    }
    return output;
}

void print_help() {
    std::cout
        << "iozi - Mini Search Engine\n\n"
        << "Usage: iozi --data DIRECTORY [--query TEXT] [--mode any|all] [--top N]\n"
        << "       iozi --help\n\n"
        << "  --data DIRECTORY  Recursively index .txt and .md files.\n"
        << "  --query TEXT      Search once; omit to enter the interactive prompt.\n"
        << "  --mode any|all    Match any keyword (default) or every keyword.\n"
        << "  --top N           Return at most N results (1-1000, default 10).\n\n"
        << "Keywords are case-insensitive ASCII letters and digits; punctuation\n"
        << "and non-ASCII bytes are separators. Queries are limited to 4096 bytes.\n"
        << "No phrase matching, stemming, web crawling or saved index.\n"
        << "Interactive commands: :help, :stats, :quit. EOF also exits.\n"
        << "Exit codes: 0 success, 1 file/input/index error, 2 usage error.\n";
}

Options parse_options(int argc, char* argv[]) {
    Options options;
    std::set<std::string> seen;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument != "--data" && argument != "--query" && argument != "--mode"
            && argument != "--top" && argument != "--help") {
            throw UsageError("Unknown option: " + argument);
        }
        if (!seen.insert(argument).second) {
            throw UsageError("Duplicate option: " + argument);
        }
        if (argument == "--help") {
            options.help = true;
            continue;
        }
        if (i + 1 == argc) {
            throw UsageError("Missing value for " + argument);
        }
        const std::string value = argv[++i];
        if (argument == "--data") {
            if (value.empty()) {
                throw UsageError("--data must not be empty");
            }
            options.directory = value;
        } else if (argument == "--query") {
            if (value.size() > max_query_bytes) {
                throw UsageError("Query exceeds 4096 bytes");
            }
            options.query = value;
        } else if (argument == "--mode") {
            if (value == "any") {
                options.search.mode = iozi::MatchMode::any;
            } else if (value == "all") {
                options.search.mode = iozi::MatchMode::all;
            } else {
                throw UsageError("--mode must be any or all");
            }
        } else if (argument == "--top") {
            std::size_t top = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), top);
            if (error != std::errc{} || end != value.data() + value.size()
                || top == 0 || top > 1000) {
                throw UsageError("--top must be an integer from 1 to 1000");
            }
            options.search.limit = top;
        }
    }
    if (options.help) {
        if (seen.size() != 1) {
            throw UsageError("--help must be used alone");
        }
    } else if (options.directory.empty()) {
        throw UsageError("--data DIRECTORY is required");
    }
    return options;
}

void print_stats(const iozi::SearchEngine& engine) {
    std::cout << "Indexed " << engine.document_count() << " documents ("
              << engine.vocabulary_size() << " terms, " << engine.total_tokens()
              << " tokens).\n";
}

bool ascii_alphanumeric(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

std::string snippet(std::string_view text, const std::unordered_set<std::string>& terms) {
    std::size_t hit = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (!ascii_alphanumeric(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        const auto begin = i;
        std::string word;
        while (i < text.size() && ascii_alphanumeric(static_cast<unsigned char>(text[i]))) {
            const auto c = text[i++];
            word.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c);
        }
        if (terms.contains(word)) {
            hit = begin;
            break;
        }
    }
    const std::size_t begin = hit > 40 ? hit - 40 : 0;
    const std::size_t end = begin + std::min<std::size_t>(160, text.size() - begin);
    std::string excerpt;
    for (std::size_t i = begin; i < end; ++i) {
        const auto c = text[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            if (!excerpt.empty() && excerpt.back() != ' ') {
                excerpt += ' ';
            }
        } else {
            excerpt += c;
        }
    }
    if (!excerpt.empty() && excerpt.back() == ' ') {
        excerpt.pop_back();
    }
    return (begin > 0 ? "..." : "") + printable(excerpt) + (end < text.size() ? "..." : "");
}

void print_search(const iozi::SearchEngine& engine, std::string_view query,
                  iozi::SearchOptions options) {
    const auto results = engine.search(query, options);
    if (results.empty()) {
        std::cout << "No matches.\n";
        return;
    }
    const auto tokens = iozi::AsciiTokenizer<>{}(query);
    const std::unordered_set<std::string> terms(tokens.begin(), tokens.end());
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        const auto& document = engine.document(result.document_id);
        std::cout << i + 1 << ". " << printable(document.key)
                  << "  [score=" << std::fixed << std::setprecision(6) << result.score << "]\n"
                  << "   " << snippet(document.text, terms) << '\n';
    }
}

std::optional<std::string> read_query(std::istream& input) {
    std::string line;
    for (;;) {
        const auto c = input.get();
        if (c == std::char_traits<char>::eof()) {
            if (input.bad() || !input.eof()) {
                throw std::runtime_error("Cannot read interactive input");
            }
            if (line.size() > max_query_bytes) {
                throw std::runtime_error("Interactive query exceeds 4096 bytes");
            }
            if (line.empty()) {
                return std::nullopt;
            }
            return line;
        }
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }
        // Permit one extra CR only so a 4096-byte query works with CRLF.
        // The next byte must be LF; EOF or any other byte still rejects it.
        if (line.size() > max_query_bytes
            || (line.size() == max_query_bytes && c != '\r')) {
            throw std::runtime_error("Interactive query exceeds 4096 bytes");
        }
        line.push_back(static_cast<char>(c));
    }
}

int run(const Options& options) {
    if (options.help) {
        print_help();
        return 0;
    }
    iozi::SearchEngine engine;
    auto corpus = iozi::load_corpus(options.directory);
    for (auto& document : corpus) {
        engine.add_document(std::move(document.key), std::move(document.text));
    }
    print_stats(engine);
    if (options.query) {
        print_search(engine, *options.query, options.search);
        return 0;
    }
    std::cout << "Enter keywords, or :help for commands.\n";
    for (;;) {
        std::cout << "iozi> " << std::flush;
        const auto query = read_query(std::cin);
        if (!query) {
            std::cout << '\n';
            break;
        }
        if (*query == ":quit") {
            break;
        }
        if (*query == ":help") {
            std::cout << "Enter keywords to search. :stats shows index statistics; :quit exits.\n";
        } else if (*query == ":stats") {
            print_stats(engine);
        } else if (!query->empty() && query->front() == ':') {
            std::cout << "Unknown command. Type :help.\n";
        } else {
            print_search(engine, *query, options.search);
        }
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run(parse_options(argc, argv));
    } catch (const UsageError& error) {
        std::cerr << "iozi: " << printable(error.what()) << "\nUse --help for usage.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "iozi: " << printable(error.what()) << '\n';
        return 1;
    }
}
