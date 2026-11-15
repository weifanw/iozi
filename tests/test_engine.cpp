#include "iozi/engine.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void near(double actual, double expected) {
    check(std::abs(actual - expected) < 1e-12,
          "Expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

template <typename Exception, typename Function>
void throws(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected exception was not thrown");
}

void tokenizer_boundaries() {
    const iozi::AsciiTokenizer<> tokenizer;
    const std::string input = "Hello, WORLD! c++ C_20 a-b 007\tMiXeD\n";
    check(tokenizer(input) == std::vector<std::string>{"hello", "world", "c", "c", "20",
                                                     "a", "b", "007", "mixed"},
          "ASCII normalization or delimiters failed");
    check(tokenizer("!!! \r\n").empty(), "Punctuation is not a token");
    check(tokenizer("").empty(), "Empty input has no tokens");
    check(tokenizer(std::string("A\0B", 3)) == std::vector<std::string>{"a", "b"},
          "Embedded NUL must delimit terms");
    check(tokenizer("caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87") ==
              std::vector<std::string>{"caf"},
          "Non-ASCII bytes must be separators");
    check(iozi::AsciiTokenizer<3>{}("a bb CCC dddd 42 123") ==
              std::vector<std::string>{"ccc", "dddd", "123"},
          "Compile-time minimum token length failed");
    // Exercise every possible byte, including negative plain-char values.
    std::string all_bytes;
    for (int byte = 0; byte < 256; ++byte) {
        all_bytes.push_back(static_cast<char>(byte));
    }
    check(tokenizer(all_bytes) == std::vector<std::string>{
              "0123456789", "abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz"},
          "The complete ASCII lookup table is incorrect");
}

void empty_index_and_documents() {
    iozi::SearchEngine engine;
    check(engine.search("anything").empty(), "Empty engine returned results");
    check(engine.document_count() == 0 && engine.total_tokens() == 0 &&
              engine.vocabulary_size() == 0, "Empty statistics are wrong");
    engine.add_document("blank", "");
    engine.add_document("punctuation", "!?.,");
    check(engine.document_count() == 2 && engine.total_tokens() == 0,
          "Blank document accounting is wrong");
    check(engine.search("word").empty(), "Zero-length corpus returned results");
    engine.add_document("one", "Word");
    const auto results = engine.search("word");
    check(results.size() == 1 && std::isfinite(results.front().score),
          "Mixed blank/nonblank corpus must have a finite result");
    // N=3, df=1, average length=1/3: idf=log(8/3), saturation=2.2/4.
    near(results.front().score, std::log(8.0 / 3.0) * 0.55);
    check(engine.search("  !!!  ").empty(), "Empty normalized query must have no results");
}

void duplicate_keys_and_document_access() {
    iozi::SearchEngine engine;
    check(engine.add_document("one", "alpha alpha beta") == 0, "First ID is wrong");
    throws<std::invalid_argument>([&] { engine.add_document("one", "gamma"); });
    throws<std::invalid_argument>([&] { engine.add_document("", "gamma"); });
    check(engine.document_count() == 1 && engine.total_tokens() == 3 &&
              engine.vocabulary_size() == 2, "Rejected add changed the index");
    check(engine.document(0).key == "one" && engine.document(0).text == "alpha alpha beta" &&
              engine.document(0).token_count == 3, "Document metadata is wrong");
    check(engine.search("gamma").empty(), "Rejected document leaked into index");
    throws<std::out_of_range>([&] { (void)engine.document(1); });
    check(engine.add_document("two", "gamma") == 1, "Rejected add consumed an ID");
}

void hand_calculated_bm25() {
    iozi::SearchEngine engine;
    engine.add_document("a", "apple apple banana");
    engine.add_document("b", "apple carrot");
    engine.add_document("c", "banana carrot carrot carrot");
    const auto results = engine.search("apple");
    check(results.size() == 2 && results[0].document_id == 0 && results[1].document_id == 1,
          "BM25 apple ranking is wrong");
    // N=3, df=2, average length=3. Natural-log IDF is log(1.6).
    // a: tf=2, dl=3 -> multiplier=4.4/3.2. b: tf=1, dl=2 -> 2.2/1.9.
    near(results[0].score, std::log(1.6) * 4.4 / 3.2);
    near(results[1].score, std::log(1.6) * 2.2 / 1.9);
    const auto both = engine.search("apple banana", {iozi::MatchMode::all, 10});
    check(both.size() == 1 && both[0].document_id == 0, "ALL matching is wrong");
    near(both[0].score, std::log(1.6) * (4.4 / 3.2 + 1.0));
    near(iozi::BM25<120, 0>{}(1, 100, 1.0, 3, 2), std::log(1.6));
    near(iozi::BM25<>{}(0, 0, 0.0, 0, 0), 0.0);
}

void query_semantics() {
    iozi::SearchEngine engine;
    engine.add_document("one", "alpha beta");
    engine.add_document("two", "beta gamma");
    engine.add_document("three", "gamma");
    check(engine.search("alpha gamma").size() == 3, "ANY must union postings");
    check(engine.search("alpha gamma", {iozi::MatchMode::all, 10}).empty(),
          "ALL must intersect postings");
    check(engine.search("beta absent").size() == 2, "ANY must ignore unknown terms");
    check(engine.search("beta absent", {iozi::MatchMode::all, 10}).empty(),
          "Unknown term must make ALL empty");
    check(engine.search("absent").empty(), "Unknown-only query returned results");
    const auto single = engine.search("beta");
    const auto repeated = engine.search("BETA beta beta", {iozi::MatchMode::all, 10});
    check(single.size() == repeated.size(), "Query deduplication changed matches");
    for (std::size_t i = 0; i < single.size(); ++i) {
        check(single[i].document_id == repeated[i].document_id, "Query dedup changed order");
        near(single[i].score, repeated[i].score);
    }
    const auto first_order = engine.search("gamma alpha beta");
    const auto second_order = engine.search("beta gamma alpha");
    check(first_order.size() == second_order.size(), "Query permutation changed count");
    for (std::size_t i = 0; i < first_order.size(); ++i) {
        check(first_order[i].document_id == second_order[i].document_id &&
                  first_order[i].score == second_order[i].score,
              "Query order must not affect exact scores or ordering");
    }
}

void ties_and_limits() {
    iozi::SearchEngine engine;
    engine.add_document("z-last", "same word");
    engine.add_document("a-first", "same word");
    engine.add_document("m-middle", "same word");
    const auto all = engine.search("same", {iozi::MatchMode::any, 100});
    check(all.size() == 3 && all[0].document_id == 1 && all[1].document_id == 2 &&
              all[2].document_id == 0, "Tie order must use keys, not insertion order");
    const auto limited = engine.search("same", {iozi::MatchMode::any, 1});
    check(limited.size() == 1 && limited[0].document_id == 1, "Limit must follow ranking");
    check(engine.search("same", {iozi::MatchMode::any, 0}).empty(), "Zero limit failed");
}

struct WholeTextTokenizer {
    std::vector<std::string> operator()(std::string_view text) const {
        return text.empty() ? std::vector<std::string>{} :
                              std::vector<std::string>{std::string(text)};
    }
};

struct ConstantRanker {
    double value = 7.0;
    double operator()(std::size_t, std::size_t, double, std::size_t, std::size_t) const {
        return value;
    }
};

struct InvalidTokenizer {
    std::vector<std::string> operator()(std::string_view) const { return {""}; }
};

static_assert(iozi::TokenizerPolicy<WholeTextTokenizer>);
static_assert(iozi::RankerPolicy<ConstantRanker>);
static_assert(!iozi::TokenizerPolicy<int>);
static_assert(!iozi::RankerPolicy<int>);

void policy_substitution() {
    iozi::BasicSearchEngine<WholeTextTokenizer, ConstantRanker> engine({}, {3.5});
    engine.add_document("phrase", "Case Sensitive");
    check(engine.search("case sensitive").empty(), "Custom tokenizer was not used");
    const auto results = engine.search("Case Sensitive");
    check(results.size() == 1, "Whole-text tokenizer did not match");
    near(results[0].score, 3.5);
    iozi::BasicSearchEngine<iozi::AsciiTokenizer<3>> long_terms;
    long_terms.add_document("words", "a to cat elephant");
    check(long_terms.total_tokens() == 2 && long_terms.search("a to").empty(),
          "Minimum-length policy must apply to documents and queries");
    iozi::BasicSearchEngine<InvalidTokenizer> invalid;
    throws<std::invalid_argument>([&] { invalid.add_document("bad", "input"); });
    check(invalid.document_count() == 0, "Invalid tokenizer corrupted the index");
}

void invalid_ranker_values() {
    iozi::BasicSearchEngine<iozi::AsciiTokenizer<>, ConstantRanker> engine(
        {}, {std::numeric_limits<double>::quiet_NaN()});
    engine.add_document("doc", "alpha");
    throws<std::domain_error>([&] { (void)engine.search("alpha"); });
    iozi::BasicSearchEngine<iozi::AsciiTokenizer<>, ConstantRanker> huge(
        {}, {std::numeric_limits<double>::max()});
    huge.add_document("doc", "alpha beta");
    throws<std::overflow_error>([&] { (void)huge.search("alpha beta"); });
}

void ranking_signals() {
    iozi::SearchEngine rare;
    rare.add_document("rare", "common unique");
    rare.add_document("common-a", "common plain");
    rare.add_document("common-b", "common plain");
    check(rare.search("common unique").front().document_id == 0,
          "A rare matching term should increase relevance");
    iozi::SearchEngine length;
    length.add_document("long", "needle padding padding padding padding padding");
    length.add_document("short", "needle");
    check(length.search("needle").front().document_id == 1,
          "Length normalization should favor a focused document at equal tf");
}
} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"tokenizer boundaries", tokenizer_boundaries},
        {"empty index and documents", empty_index_and_documents},
        {"duplicate keys and document access", duplicate_keys_and_document_access},
        {"hand-calculated BM25", hand_calculated_bm25},
        {"query semantics", query_semantics},
        {"ties and limits", ties_and_limits},
        {"policy substitution", policy_substitution},
        {"invalid ranker values", invalid_ranker_values},
        {"ranking signals", ranking_signals},
    };
    std::size_t failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " engine tests passed\n";
    return failures == 0 ? 0 : 1;
}
