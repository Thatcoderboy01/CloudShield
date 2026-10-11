
#include "http/http_request.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <string_view>
#include <system_error>

namespace cloudshield::http {
namespace {

constexpr std::size_t MAX_REQUEST_BYTES = 4096;
constexpr std::size_t MAX_HEADER_BYTES = 2048;

ParseResult badRequest(std::string message) {
    return {
        ParseStatus::BadRequest,
        {},
        std::move(message)
    };
}

bool isTokenChar(unsigned char c) {
    if (std::isalnum(c)) {
        return true;
    }

    constexpr std::string_view special = "!#$%&'*+-.^_`|~";

    return special.find(static_cast<char>(c)) !=
           std::string_view::npos;
}

bool isValidToken(std::string_view value) {
    if (value.empty()) {
        return false;
    }

    for (unsigned char c : value) {
        if (!isTokenChar(c)) {
            return false;
        }
    }

    return true;
}

std::string toLower(std::string_view value) {
    std::string result;
    result.reserve(value.size());

    for (unsigned char c : value) {
        result.push_back(
            static_cast<char>(std::tolower(c))
        );
    }

    return result;
}

std::string_view trimWhitespace(std::string_view value) {
    while (!value.empty() &&
           (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }

    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }

    return value;
}

bool containsInvalidHeaderValueChar(std::string_view value) {
    for (unsigned char c : value) {
        // HTAB is allowed; other control characters are rejected.
        if ((c < 32 && c != '\t') || c == 127) {
            return true;
        }
    }

    return false;
}

}  // namespace

ParseResult parseHttpRequest(std::string_view raw) {
    if (raw.size() > MAX_REQUEST_BYTES) {
        return badRequest("Request exceeds size limit");
    }

    // Find the end of the HTTP headers.
    const std::size_t header_end = raw.find("\r\n\r\n");

    if (header_end == std::string_view::npos) {
        // This may be a partial request. The caller must continue
        // buffering until more data arrives or a limit is reached.
        return {
            ParseStatus::Incomplete,
            {},
            "Waiting for complete HTTP headers"
        };
    }

    if (header_end + 4 > MAX_HEADER_BYTES) {
        return badRequest("HTTP headers exceed size limit");
    }

    const std::size_t request_line_end = raw.find("\r\n");

    if (request_line_end == std::string_view::npos ||
        request_line_end >= header_end) {
        return badRequest("Invalid HTTP request line");
    }

    const std::string_view request_line =
        raw.substr(0, request_line_end);

    // Request line must contain exactly three parts:
    // METHOD SP TARGET SP VERSION
    const std::size_t first_space = request_line.find(' ');

    if (first_space == std::string_view::npos ||
        first_space == 0) {
        return badRequest("Malformed HTTP request line");
    }

    const std::size_t second_space =
        request_line.find(' ', first_space + 1);

    if (second_space == std::string_view::npos ||
        second_space == first_space + 1 ||
        second_space == request_line.size() - 1 ||
        request_line.find(' ', second_space + 1) !=
            std::string_view::npos) {
        return badRequest("Malformed HTTP request line");
    }

    const std::string_view method =
        request_line.substr(0, first_space);

    const std::string_view target =
        request_line.substr(
            first_space + 1,
            second_space - first_space - 1
        );

    const std::string_view version =
        request_line.substr(second_space + 1);

    if (!isValidToken(method)) {
        return badRequest("Invalid HTTP method");
    }

    if (target.empty()) {
        return badRequest("Missing request target");
    }

    for (unsigned char c : target) {
        if (c <= 32 || c == 127) {
            return badRequest("Invalid character in request target");
        }
    }

    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        return badRequest("Unsupported HTTP version");
    }

    HttpRequest request;
    request.method = std::string(method);
    request.target = std::string(target);
    request.version = std::string(version);

    // Parse headers between the request line and the blank line.
    const std::size_t headers_start = request_line_end + 2;

    const std::string_view header_block =
        raw.substr(
            headers_start,
            header_end - headers_start
        );

    std::size_t position = 0;
    bool has_content_length = false;
    std::size_t content_length = 0;

    while (position < header_block.size()) {
        const std::size_t line_end =
            header_block.find("\r\n", position);

        const std::size_t end =
            line_end == std::string_view::npos
                ? header_block.size()
                : line_end;

        const std::string_view line =
            header_block.substr(position, end - position);

        if (line.empty()) {
            return badRequest("Unexpected empty header line");
        }

        // Obsolete folded headers are not supported.
        if (line.front() == ' ' || line.front() == '\t') {
            return badRequest("Folded headers are not supported");
        }

        const std::size_t colon = line.find(':');

        if (colon == std::string_view::npos || colon == 0) {
            return badRequest("Malformed HTTP header");
        }

        const std::string_view name =
            line.substr(0, colon);

        const std::string_view value =
            trimWhitespace(line.substr(colon + 1));

        if (!isValidToken(name)) {
            return badRequest("Invalid HTTP header name");
        }

        if (containsInvalidHeaderValueChar(value)) {
            return badRequest("Invalid HTTP header value");
        }

        const std::string key = toLower(name);

        // Chunked transfer encoding is deliberately unsupported
        // in this first parser implementation.
        if (key == "transfer-encoding") {
            return badRequest(
                "Transfer-Encoding is not supported yet"
            );
        }

        if (key == "content-length") {
            if (has_content_length) {
                return badRequest(
                    "Duplicate Content-Length header"
                );
            }

            if (value.empty()) {
                return badRequest("Empty Content-Length");
            }

            std::size_t parsed_length = 0;

            const char* begin = value.data();
            const char* end_ptr = begin + value.size();

            const auto result = std::from_chars(
                begin,
                end_ptr,
                parsed_length
            );

            if (result.ec != std::errc{} ||
                result.ptr != end_ptr) {
                return badRequest("Invalid Content-Length");
            }

            content_length = parsed_length;
            has_content_length = true;
        }

        // Combine repeated ordinary headers into a comma-separated
        // value. Content-Length duplicates were rejected above.
        auto [it, inserted] = request.headers.try_emplace(
            key,
            value
        );

        if (!inserted) {
            it->second += ", ";
            it->second.append(value);
        }

        if (line_end == std::string_view::npos) {
            break;
        }

        position = line_end + 2;
    }

    if (request.version == "HTTP/1.1" &&
        request.headers.find("host") == request.headers.end()) {
        return badRequest("HTTP/1.1 requires a Host header");
    }

    const std::size_t body_start = header_end + 4;

    // Avoid overflow and reject requests that exceed our limit.
    if (content_length > MAX_REQUEST_BYTES - body_start) {
        return badRequest("Request body exceeds size limit");
    }

    const std::size_t expected_size =
        body_start + content_length;

    if (raw.size() < expected_size) {
        return {
            ParseStatus::Incomplete,
            {},
            "Waiting for complete request body"
        };
    }

    // This initial parser handles one request per connection.
    // HTTP pipelining and extra trailing bytes are not supported.
    if (raw.size() > expected_size) {
        return badRequest(
            "Extra bytes after request are not supported"
        );
    }

    request.body = std::string(
        raw.substr(body_start, content_length)
    );

    return {
        ParseStatus::Complete,
        std::move(request),
        {}
    };
}

}  // namespace cloudshield::http
