#include "proxy/backend_response.hpp"

#include <charconv>
#include <cctype>
#include <string>
#include <string_view>
#include <system_error>

namespace cloudshield::proxy {
namespace {

constexpr std::size_t MAX_RESPONSE_BYTES = 1024 * 1024;
constexpr std::size_t MAX_HEADER_BYTES = 16 * 1024;

BackendParseResult badResponse(std::string message) {
    return {
        BackendParseStatus::BadResponse,
        {},
        std::move(message)
    };
}

std::string lower(std::string_view value) {
    std::string result;
    result.reserve(value.size());

    for (unsigned char c : value) {
        result.push_back(
            static_cast<char>(std::tolower(c))
        );
    }

    return result;
}

std::string_view trim(std::string_view value) {
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

bool validHeaderName(std::string_view name) {
    if (name.empty()) {
        return false;
    }

    constexpr std::string_view special =
        "!#$%&'*+-.^_`|~";

    for (unsigned char c : name) {
        if (!std::isalnum(c) &&
            special.find(static_cast<char>(c)) ==
                std::string_view::npos) {
            return false;
        }
    }

    return true;
}

bool validHeaderValue(std::string_view value) {
    for (unsigned char c : value) {
        if ((c < 32 && c != '\t') || c == 127) {
            return false;
        }
    }

    return true;
}

}  // namespace

BackendParseResult parseBackendResponse(std::string_view raw) {
    if (raw.size() > MAX_RESPONSE_BYTES) {
        return badResponse("Backend response exceeds size limit");
    }

    const std::size_t header_end = raw.find("\r\n\r\n");

    if (header_end == std::string_view::npos) {
        if (raw.size() > MAX_HEADER_BYTES) {
            return badResponse("Backend headers exceed size limit");
        }

        return {
            BackendParseStatus::Incomplete,
            {},
            "Waiting for complete backend headers"
        };
    }

    if (header_end + 4 > MAX_HEADER_BYTES) {
        return badResponse("Backend headers exceed size limit");
    }

    const std::size_t status_end = raw.find("\r\n");

    if (status_end == std::string_view::npos ||
        status_end >= header_end) {
        return badResponse("Invalid backend status line");
    }

    const std::string_view status_line =
        raw.substr(0, status_end);

    if (!status_line.starts_with("HTTP/1.1 ") &&
        !status_line.starts_with("HTTP/1.0 ")) {
        return badResponse("Unsupported backend HTTP version");
    }

    if (status_line.size() < 12 ||
        status_line[9] < '1' ||
        status_line[9] > '5' ||
        status_line[10] < '0' ||
        status_line[10] > '9' ||
        status_line[11] < '0' ||
        status_line[11] > '9') {
        return badResponse("Invalid backend status code");
    }

    const int status_code =
        (status_line[9] - '0') * 100 +
        (status_line[10] - '0') * 10 +
        (status_line[11] - '0');

    if (status_code < 100 || status_code > 599) {
        return badResponse("Backend status code out of range");
    }

    cloudshield::http::HttpResponse response;
    response.status_code = status_code;
    response.reason_phrase = status_line.size() > 12
        ? std::string(trim(status_line.substr(12)))
        : "";

    bool has_content_length = false;
    std::size_t content_length = 0;

    std::size_t position = status_end + 2;

    while (position < header_end) {
        const std::size_t line_end = raw.find("\r\n", position);

        if (line_end == std::string_view::npos ||
            line_end > header_end) {
            return badResponse("Malformed backend header line");
        }

        const std::string_view line =
            raw.substr(position, line_end - position);

        const std::size_t colon = line.find(':');

        if (colon == std::string_view::npos || colon == 0) {
            return badResponse("Malformed backend header");
        }

        const std::string_view name = line.substr(0, colon);
        const std::string_view value = trim(line.substr(colon + 1));

        if (!validHeaderName(name) || !validHeaderValue(value)) {
            return badResponse("Invalid backend header");
        }

        const std::string key = lower(name);

        if (key == "transfer-encoding") {
            return badResponse(
                "Chunked backend responses are not supported yet"
            );
        }

        if (key == "content-length") {
            if (has_content_length || value.empty()) {
                return badResponse("Invalid backend Content-Length");
            }

            const char* begin = value.data();
            const char* end = begin + value.size();

            const auto parsed = std::from_chars(
                begin, end, content_length
            );

            if (parsed.ec != std::errc{} || parsed.ptr != end) {
                return badResponse("Invalid backend Content-Length");
            }

            has_content_length = true;
        }

        if (key == "content-type") {
            response.content_type = std::string(value);
        } else if (key != "connection" &&
                   key != "content-length") {
            response.headers[std::string(name)] =
                std::string(value);
        }

        position = line_end + 2;
    }

    // This initial parser requires explicit Content-Length.
    // Responses framed by connection close or chunked encoding
    // will need separate support.
    if (!has_content_length) {
        return badResponse("Backend response requires Content-Length");
    }

    const std::size_t body_start = header_end + 4;

    if (content_length > MAX_RESPONSE_BYTES - body_start) {
        return badResponse("Backend response body exceeds size limit");
    }

    const std::size_t expected_size = body_start + content_length;

    if (raw.size() < expected_size) {
        return {
            BackendParseStatus::Incomplete,
            {},
            "Waiting for complete backend body"
        };
    }

    if (raw.size() > expected_size) {
        return badResponse("Unexpected bytes after backend response");
    }

    response.body = std::string(raw.substr(body_start, content_length));
    response.connection = "close";

    return {
        BackendParseStatus::Complete,
        std::move(response),
        {}
    };
}

}  // namespace cloudshield::proxy