#include "proxy/reverse_proxy.hpp"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>


#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>


namespace cloudshield::proxy {

ConnectResult startBackendConnection(
    const char* address,
    std::uint16_t port
) {
    if (address == nullptr) {
        return {ConnectStatus::Failed, -1, EINVAL};
    }

    const int fd = socket(
        AF_INET,
        SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
        0
    );

    if (fd == -1) {
        return {ConnectStatus::Failed, -1, errno};
    }

    sockaddr_in backend_addr{};
    backend_addr.sin_family = AF_INET;
    backend_addr.sin_port = htons(port);

    if (inet_pton(
            AF_INET,
            address,
            &backend_addr.sin_addr
        ) != 1) {
        const int error = errno == 0 ? EINVAL : errno;
        close(fd);
        return {ConnectStatus::Failed, -1, error};
    }

    if (connect(
            fd,
            reinterpret_cast<sockaddr*>(&backend_addr),
            sizeof(backend_addr)
        ) == 0) {
        return {ConnectStatus::Connected, fd, 0};
    }

    if (errno == EINPROGRESS) {
        return {ConnectStatus::InProgress, fd, 0};
    }

    const int error = errno;
    close(fd);

    return {ConnectStatus::Failed, -1, error};
}


std::string buildBackendRequest(
    const cloudshield::http::HttpRequest& request
) {
    std::string output;

    output.reserve(
        request.method.size() +
        request.target.size() +
        request.body.size() + 256
    );

    output += request.method;
    output += " ";
    output += request.target;
    output += " HTTP/1.1\r\n";

    // Headers that must not be forwarded by a proxy.
    static const std::unordered_set<std::string> blocked = {
        "connection",
        "keep-alive",
        "proxy-authenticate",
        "proxy-authorization",
        "te",
        "trailer",
        "transfer-encoding",
        "upgrade",
        "host",
        "content-length"
    };

    // Connection can nominate additional hop-by-hop headers.
    std::unordered_set<std::string> excluded = blocked;

    for (const auto& [name, value] : request.headers) {
        if (name == "connection") {
            std::string token;

            for (char ch : value) {
                if (ch == ',') {
                    if (!token.empty()) {
                        excluded.insert(token);
                        token.clear();
                    }
                } else if (!std::isspace(
                               static_cast<unsigned char>(ch))) {
                    token += static_cast<char>(
                        std::tolower(static_cast<unsigned char>(ch))
                    );
                }
            }

            if (!token.empty()) {
                excluded.insert(token);
            }
        }
    }

    for (const auto& [name, value] : request.headers) {
        if (excluded.contains(name)) {
            continue;
        }

        output += name;
        output += ": ";
        output += value;
        output += "\r\n";
    }

    output += "Host: 127.0.0.1\r\n";
    output += "Content-Length: ";
    output += std::to_string(request.body.size());
    output += "\r\n";
    output += "Connection: close\r\n";
    output += "\r\n";
    output += request.body;

    return output;
}


}  // namespace cloudshield::proxy