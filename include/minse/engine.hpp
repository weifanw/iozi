#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace minse {

namespace detail {
constexpr unsigned char normalized_ascii(std::size_t byte) noexcept {
    if (byte >= 'A' && byte <= 'Z') {
        return static_cast<unsigned char>(byte + ('a' - 'A'));
    }
    if ((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')) {
        return static_cast<unsigned char>(byte);
    }
    return 0;
}

// Expand the character classification/normalization table at compile time.
template <std::size_t... Bytes>
constexpr auto make_ascii_table(std::index_sequence<Bytes...>) noexcept {
    return std::array<unsigned char, sizeof...(Bytes)>{normalized_ascii(Bytes)...};
}

inline constexpr auto ascii_table = make_ascii_table(std::make_index_sequence<256>{});
} // namespace detail

// ASCII letters/digits form terms; all other bytes are separators. This is
// deliberately not a Unicode tokenizer. No locale or platform ctype state.
template <std::size_t MinTokenLength = 1>
class AsciiTokenizer {
    static_assert(MinTokenLength > 0, "Minimum token length must be positive");

public:
    [[nodiscard]] std::vector<std::string> operator()(std::string_view text) const {
        std::vector<std::string> terms;
        std::string current;
        const auto flush = [&] {
            if (current.size() >= MinTokenLength) {
                terms.push_back(current);
            }
            current.clear();
        };
        for (unsigned char byte : text) {
            const auto normalized = detail::ascii_table[byte];
            if (normalized != 0) {
                current.push_back(static_cast<char>(normalized));
            } else {
                flush();
            }
        }
        flush();
        return terms;
    }
};

// Integer template arguments make configuration portable across C++20
// compilers. BM25<120, 75> means k1=1.2 and b=0.75.
template <int K1Percent = 120, int BPercent = 75>
struct BM25 {
    static_assert(K1Percent > 0, "BM25 k1 must be positive");
    static_assert(BPercent >= 0 && BPercent <= 100, "BM25 b must be in [0, 1]");

    [[nodiscard]] double operator()(std::size_t term_frequency,
                                    std::size_t document_length,
                                    double average_document_length,
                                    std::size_t document_count,
                                    std::size_t document_frequency) const noexcept {
        if (term_frequency == 0 || document_count == 0 || document_frequency == 0 ||
            document_frequency > document_count || average_document_length <= 0.0) {
            return 0.0;
        }
        constexpr double k1 = static_cast<double>(K1Percent) / 100.0;
        constexpr double b = static_cast<double>(BPercent) / 100.0;
        const double tf = static_cast<double>(term_frequency);
        const double n = static_cast<double>(document_count);
        const double df = static_cast<double>(document_frequency);
        const double idf = std::log1p((n - df + 0.5) / (df + 0.5));
        const double length_ratio = static_cast<double>(document_length) /
                                    average_document_length;
        return idf * (tf * (k1 + 1.0)) / (tf + k1 * (1.0 - b + b * length_ratio));
    }
};

template <typename T>
concept TokenizerPolicy = requires(const T& tokenizer, std::string_view text) {
    { tokenizer(text) } -> std::same_as<std::vector<std::string>>;
};

template <typename T>
concept RankerPolicy = requires(const T& ranker, std::size_t count, double average) {
    { ranker(count, count, average, count, count) } -> std::convertible_to<double>;
};

struct Document {
    std::string key;
    std::string text;
    std::size_t token_count = 0;
};

struct SearchResult {
    std::size_t document_id;
    double score;
};

enum class MatchMode { any, all };

struct SearchOptions {
    MatchMode mode = MatchMode::any;
    std::size_t limit = 10;
};

template <TokenizerPolicy Tokenizer = AsciiTokenizer<>, RankerPolicy Ranker = BM25<>>
class BasicSearchEngine {
public:
    explicit BasicSearchEngine(Tokenizer tokenizer = {}, Ranker ranker = {})
        : tokenizer_(std::move(tokenizer)), ranker_(std::move(ranker)) {}

    // Keys must be nonempty and unique. IDs stay stable until destruction;
    // references returned by document() can be invalidated by the next add.
    std::size_t add_document(std::string key, std::string text) {
        if (key.empty()) {
            throw std::invalid_argument("Document key must not be empty");
        }
        if (keys_.contains(key)) {
            throw std::invalid_argument("Duplicate document key: " + key);
        }
        const auto tokens = tokenizer_(text);
        if (tokens.size() > std::numeric_limits<std::size_t>::max() - total_tokens_) {
            throw std::length_error("Total token count exceeds size_t capacity");
        }
        std::map<std::string, std::size_t> frequencies;
        for (const auto& token : tokens) {
            if (token.empty()) {
                throw std::invalid_argument("Tokenizer emitted an empty term");
            }
            ++frequencies[token];
        }
        const auto id = documents_.size();
        const auto [key_position, inserted] = keys_.emplace(key, id);
        (void)inserted; // Uniqueness checked above; this class is not thread-safe.
        try {
            documents_.push_back({std::move(key), std::move(text), tokens.size()});
            for (const auto& [term, frequency] : frequencies) {
                index_[term].push_back({id, frequency});
            }
        } catch (...) {
            // Roll back partially added postings if an allocation fails.
            for (const auto& [term, frequency] : frequencies) {
                (void)frequency;
                const auto entry = index_.find(term);
                if (entry != index_.end()) {
                    auto& postings = entry->second;
                    if (!postings.empty() && postings.back().document_id == id) {
                        postings.pop_back();
                    }
                    if (postings.empty()) {
                        index_.erase(entry);
                    }
                }
            }
            if (documents_.size() > id) {
                documents_.pop_back();
            }
            keys_.erase(key_position);
            throw;
        }
        total_tokens_ += tokens.size();
        return id;
    }

    [[nodiscard]] std::vector<SearchResult> search(std::string_view query,
                                                  SearchOptions options = {}) const {
        if (options.limit == 0 || documents_.empty() || total_tokens_ == 0) {
            return {};
        }
        auto terms = tokenizer_(query);
        std::sort(terms.begin(), terms.end());
        terms.erase(std::unique(terms.begin(), terms.end()), terms.end());
        if (terms.empty()) {
            return {};
        }
        const double average = static_cast<double>(total_tokens_) /
                               static_cast<double>(documents_.size());
        struct Accumulator {
            double score = 0.0;
            std::size_t matched_terms = 0;
        };
        std::unordered_map<std::size_t, Accumulator> candidates;
        // Sorted query terms give deterministic floating-point addition order.
        for (const auto& term : terms) {
            const auto entry = index_.find(term);
            if (entry == index_.end()) {
                if (options.mode == MatchMode::all) {
                    return {};
                }
                continue;
            }
            const auto& postings = entry->second;
            for (const auto& posting : postings) {
                const double score = static_cast<double>(ranker_(
                    posting.term_frequency, documents_[posting.document_id].token_count,
                    average, documents_.size(), postings.size()));
                if (!std::isfinite(score)) {
                    throw std::domain_error("Ranker returned a non-finite score");
                }
                auto& candidate = candidates[posting.document_id];
                candidate.score += score;
                if (!std::isfinite(candidate.score)) {
                    throw std::overflow_error("Combined relevance score is not finite");
                }
                ++candidate.matched_terms;
            }
        }
        std::vector<SearchResult> results;
        results.reserve(candidates.size());
        for (const auto& [id, candidate] : candidates) {
            if (options.mode == MatchMode::any || candidate.matched_terms == terms.size()) {
                results.push_back({id, candidate.score});
            }
        }
        std::sort(results.begin(), results.end(), [this](const auto& lhs, const auto& rhs) {
            if (lhs.score != rhs.score) {
                return lhs.score > rhs.score;
            }
            return documents_[lhs.document_id].key < documents_[rhs.document_id].key;
        });
        if (results.size() > options.limit) {
            results.resize(options.limit);
        }
        return results;
    }

    [[nodiscard]] const Document& document(std::size_t id) const { return documents_.at(id); }
    [[nodiscard]] std::size_t document_count() const noexcept { return documents_.size(); }
    [[nodiscard]] std::size_t vocabulary_size() const noexcept { return index_.size(); }
    [[nodiscard]] std::size_t total_tokens() const noexcept { return total_tokens_; }

private:
    struct Posting {
        std::size_t document_id;
        std::size_t term_frequency;
    };
    Tokenizer tokenizer_;
    Ranker ranker_;
    std::vector<Document> documents_;
    std::unordered_map<std::string, std::size_t> keys_;
    std::unordered_map<std::string, std::vector<Posting>> index_;
    std::size_t total_tokens_ = 0;
};

using SearchEngine = BasicSearchEngine<>;

} // namespace minse
