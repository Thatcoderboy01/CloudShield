
#include "http/http_response.hpp"

#include <sstream>
#include <string>

namespace cloudshield::http {

namespace {

std::string reasonPhraseFor(int status_code) {
    switch (status_code) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 413:
            return "Content Too Large";
        case 429:
            return "Too Many Requests";
        case 500:
            return "Internal Server Error";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        default:
            return "Unknown";
    }
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

    output << "Content-Type: "
           << response.content_type << "\r\n";

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
