#include "http/http_response.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace cloudshield::http {
namespace {

std::string reasonPhraseFor(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Content Too Large";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default: return "Unknown";
    }
}

bool validHeaderName(std::string_view name) {
    if (name.empty()) {
        return false;
    }

    constexpr std::string_view allowed =
        "!#$%&'*+-.^_`|~";

    for (unsigned char c : name) {
        if (!std::isalnum(c) &&
            allowed.find(static_cast<char>(c)) ==
                std::string_view::npos) {
            return false;
        }
    }

    return true;
}

bool validHeaderValue(std::string_view value) {
    for (unsigned char c : value) {
        // Prevent CRLF injection and reject control characters.
        if ((c < 32 && c != '\t') || c == 127) {
            return false;
        }
    }

    return true;
}

}  // namespace

std::string serializeHttpResponse(const HttpResponse& response) {
    const std::string reason =
        response.reason_phrase.empty()
            ? reasonPhraseFor(response.status_code)
            : response.reason_phrase;

    std::ostringstream output;

    output << "HTTP/1.1 "
           << response.status_code << ' '
           << reason << "\r\n";

    bool has_content_type = false;

    for (const auto& [name, value] : response.headers) {
        if (!validHeaderName(name) || !validHeaderValue(value)) {
            continue;
        }

        std::string lower_name = name;
        for (char& c : lower_name) {
            c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))
            );
        }

        // These are emitted from the canonical fields below.
        // Ignore caller-supplied versions to prevent duplicates.
        if (lower_name == "content-length" ||
            lower_name == "connection" ||
            lower_name == "transfer-encoding") {
            continue;
        }

        if (lower_name == "content-type") {
            has_content_type = true;
        }

        output << name << ": " << value << "\r\n";
    }

    if (!has_content_type) {
        output << "Content-Type: "
               << response.content_type << "\r\n";
    }

    output << "Content-Length: "
           << response.body.size() << "\r\n";

    output << "Connection: "
           << response.connection << "\r\n";

    output << "\r\n";
    output << response.body;

    return output.str();
}

HttpResponse makeErrorResponse(
    int status_code,
    std::string_view message
) {
    HttpResponse response;
    response.status_code = status_code;
    response.reason_phrase = reasonPhraseFor(status_code);
    response.content_type = "text/plain; charset=utf-8";
    response.body = std::string(message);
    response.connection = "close";

    return response;
}

}  // namespace cloudshield::http