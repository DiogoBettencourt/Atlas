#pragma once

// Internal helper for the streaming backends (not part of the public API):
// turns the arbitrary byte chunks an HTTP client hands over into complete
// lines, since both Ollama (NDJSON) and OpenAI-style servers (SSE) frame
// their streams by line, and a chunk can end in the middle of one.

#include <cstddef>
#include <functional>
#include <string>

namespace atlas::agent {

class StreamLines {
public:
    using LineFn = std::function<void(const std::string&)>;

    void feed(const char* data, std::size_t len, const LineFn& on_line) {
        buffer_.append(data, len);
        std::size_t start = 0;
        for (;;) {
            std::size_t newline = buffer_.find('\n', start);
            if (newline == std::string::npos) break;
            emit(buffer_.substr(start, newline - start), on_line);
            start = newline + 1;
        }
        buffer_.erase(0, start);
    }

    // Call once the stream ends: delivers a final line that had no trailing newline.
    void flush(const LineFn& on_line) {
        if (!buffer_.empty()) emit(buffer_, on_line);
        buffer_.clear();
    }

private:
    static void emit(std::string line, const LineFn& on_line) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) on_line(line);
    }

    std::string buffer_;
};

} // namespace atlas::agent
