#include "iozi/engine.hpp"

#include <emscripten/bind.h>

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr std::size_t max_documents = 100;
constexpr std::size_t max_file_bytes = 1024 * 1024;
constexpr std::size_t max_total_bytes = 10 * 1024 * 1024;
constexpr std::size_t max_query_bytes = 4096;

// Every bound function returns normally so Embind can release its converted
// string arguments. Throwing a JavaScript error from a C++ frame bypasses that
// cleanup; the JavaScript adapter turns these error envelopes into Error objects.
std::string error_json(const char* message) {
    constexpr char hex[] = "0123456789abcdef";
    std::string output = "{\"error\":\"";
    for (const unsigned char byte : std::string_view(message)) {
        if (byte == '"' || byte == '\\') {
            output.push_back('\\');
            output.push_back(static_cast<char>(byte));
        } else if (byte < 32) {
            output += "\\u00";
            output.push_back(hex[byte >> 4]);
            output.push_back(hex[byte & 15]);
        } else {
            output.push_back(static_cast<char>(byte));
        }
    }
    output += "\"}";
    return output;
}

template <typename Function>
std::string checked_json(Function&& operation) {
    try {
        return std::forward<Function>(operation)();
    } catch (const std::exception& error) {
        return error_json(error.what());
    }
}

class WebEngine {
public:
    std::string add_document_result(std::string key, std::string text) {
        return checked_json([&] {
            if (key.size() > 4096 || key.find('\0') != std::string::npos) {
                throw std::invalid_argument("Document name must be at most 4096 bytes and contain no NUL byte");
            }
            if (engine_.document_count() >= max_documents) {
                throw std::length_error("A collection can contain at most 100 documents");
            }
            if (text.size() > max_file_bytes) {
                throw std::length_error("Each document must be at most 1 MiB");
            }
            if (text.size() > max_total_bytes - total_bytes_) {
                throw std::length_error("A collection can contain at most 10 MiB of text");
            }
            if (text.find('\0') != std::string::npos) {
                throw std::invalid_argument("Binary files containing NUL bytes are not supported");
            }
            const auto bytes = text.size();
            const auto id = engine_.add_document(std::move(key), std::move(text));
            total_bytes_ += bytes;
            return "{\"id\":" + std::to_string(id) + "}";
        });
    }

    std::string search_json(const std::string& query, bool all, double requested_limit) const {
        return checked_json([&] {
            if (query.size() > max_query_bytes) {
                throw std::length_error("Queries must be at most 4096 bytes");
            }
            if (!std::isfinite(requested_limit) || requested_limit < 0.0 ||
                requested_limit > static_cast<double>(max_documents) ||
                std::floor(requested_limit) != requested_limit) {
                throw std::invalid_argument("Result limit must be an integer from 0 to 100");
            }
            const auto limit = static_cast<std::size_t>(requested_limit);
            const auto matches = engine_.search(query, {
                all ? iozi::MatchMode::all : iozi::MatchMode::any,
                engine_.document_count()});
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << std::setprecision(17) << "{\"results\":[";
            const auto count = std::min(limit, matches.size());
            for (std::size_t index = 0; index < count; ++index) {
                if (index != 0) {
                    output << ',';
                }
                output << "{\"id\":" << matches[index].document_id
                       << ",\"score\":" << matches[index].score << '}';
            }
            output << "],\"total\":" << matches.size() << '}';
            return output.str();
        });
    }

    std::string stats_json() const {
        return checked_json([&] {
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << "{\"documents\":" << engine_.document_count()
                   << ",\"terms\":" << engine_.vocabulary_size()
                   << ",\"tokens\":" << engine_.total_tokens() << '}';
            return output.str();
        });
    }

private:
    iozi::SearchEngine engine_;
    std::size_t total_bytes_ = 0;
};

} // namespace

EMSCRIPTEN_BINDINGS(iozi_web) {
    emscripten::class_<WebEngine>("NativeEngine")
        .constructor<>()
        .function("addDocumentResult", &WebEngine::add_document_result)
        .function("searchJson", &WebEngine::search_json)
        .function("statsJson", &WebEngine::stats_json);
}
