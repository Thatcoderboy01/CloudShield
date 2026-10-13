#include "http/http_response.hpp"
#include "network/tcp_server.hpp"
#include "http/http_request.hpp"
#include "router/http_router.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace {

constexpr int PORT = 8080;
constexpr int BACKLOG = 10;
constexpr int MAX_EVENTS = 64;
constexpr std::size_t MAX_INPUT_BYTES = 4096;

struct ClientState {
    std::string input;
    std::string output;

    std::size_t sent = 0;
    bool read_closed = false;
    bool response_ready = false;
};

void prepareResponse(
    ClientState& client,
    int status,
    std::string_view reason,
    std::string_view body
) {
    cloudshield::http::HttpResponse response;

    response.status_code = status;
    response.reason_phrase = std::string(reason);
    response.body = std::string(body);

    client.output =
        cloudshield::http::serializeHttpResponse(response);

    client.sent = 0;
    client.response_ready = true;
}

void removeClient(
    int epoll_fd,
    int client_fd,
    std::unordered_map<int, ClientState>& clients
) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    clients.erase(client_fd);
}

bool updateInterest(
    int epoll_fd,
    int client_fd,
    const ClientState& client
) {
    epoll_event event{};

    event.events = EPOLLRDHUP;

    if (!client.read_closed && !client.response_ready) {
        event.events |= EPOLLIN;
    }

    if (client.sent < client.output.size()) {
        event.events |= EPOLLOUT;
    }

    event.data.fd = client_fd;

    return epoll_ctl(
        epoll_fd,
        EPOLL_CTL_MOD,
        client_fd,
        &event
    ) != -1;
}

void processHttpRequest(ClientState& client) {
    const auto result =
        cloudshield::http::parseHttpRequest(client.input);

    using cloudshield::http::ParseStatus;

    if (result.status == ParseStatus::BadRequest) {
        prepareResponse(
            client,
            400,
            "Bad Request",
            "CloudShield: malformed HTTP request.\n"
        );
        return;
    }

    if (result.status == ParseStatus::Incomplete) {
        if (client.read_closed) {
            prepareResponse(
                client,
                400,
                "Bad Request",
                "CloudShield: incomplete HTTP request.\n"
            );
        }

        return;
    }

    if (result.status == ParseStatus::Complete) {
        std::cout
            << "HTTP request parsed: "
            << result.request.method << " "
            << result.request.target << '\n';

        const auto response =
            cloudshield::router::routeRequest(result.request);

        client.output =
            cloudshield::http::serializeHttpResponse(response);

        client.sent = 0;
        client.response_ready = true;
    }
}

}  // namespace

int runTcpServer() {
    int server_fd = socket(
        AF_INET,
        SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
        0
    );

    if (server_fd == -1) {
        std::cerr << "socket() failed: "
                  << std::strerror(errno) << '\n';
        return 1;
    }

    int reuse = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)
        ) == -1) {
        std::cerr << "setsockopt() failed: "
                  << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(
            server_fd,
            reinterpret_cast<sockaddr*>(&server_addr),
            sizeof(server_addr)
        ) == -1) {
        std::cerr << "bind() failed: "
                  << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, BACKLOG) == -1) {
        std::cerr << "listen() failed: "
                  << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd == -1) {
        std::cerr << "epoll_create1() failed: "
                  << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    epoll_event server_event{};
    server_event.events = EPOLLIN;
    server_event.data.fd = server_fd;

    if (epoll_ctl(
            epoll_fd,
            EPOLL_CTL_ADD,
            server_fd,
            &server_event
        ) == -1) {
        std::cerr << "epoll_ctl() failed: "
                  << std::strerror(errno) << '\n';
        close(epoll_fd);
        close(server_fd);
        return 1;
    }

    std::unordered_map<int, ClientState> clients;

    std::cout << "CloudShield HTTP server listening on port "
              << PORT << '\n';
    std::cout << "Readiness monitoring: epoll\n";
    std::cout << "Input limit: "
              << MAX_INPUT_BYTES << " bytes per connection\n";
    std::cout << "HTTP parser integration: enabled\n";
    std::cout << "Non-blocking I/O: enabled\n";
    std::cout << "Press Ctrl+C to stop.\n";

    epoll_event events[MAX_EVENTS];

    while (true) {
        int count = epoll_wait(
            epoll_fd,
            events,
            MAX_EVENTS,
            -1
        );

        if (count == -1) {
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "epoll_wait() failed: "
                      << std::strerror(errno) << '\n';
            break;
        }

        for (int i = 0; i < count; ++i) {
            const int fd = events[i].data.fd;
            const std::uint32_t flags = events[i].events;

            if (fd == server_fd) {
                if (flags & (EPOLLERR | EPOLLHUP)) {
                    std::cerr << "Listening socket error\n";
                    goto shutdown;
                }

                if (!(flags & EPOLLIN)) {
                    continue;
                }

                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);

                    int client_fd = accept4(
                        server_fd,
                        reinterpret_cast<sockaddr*>(&client_addr),
                        &client_len,
                        SOCK_NONBLOCK | SOCK_CLOEXEC
                    );

                    if (client_fd == -1) {
                        if (errno == EINTR) {
                            continue;
                        }

                        if (errno == EAGAIN ||
                            errno == EWOULDBLOCK) {
                            break;
                        }

                        std::cerr << "accept4() failed: "
                                  << std::strerror(errno) << '\n';
                        break;
                    }

                    epoll_event client_event{};
                    client_event.events = EPOLLIN | EPOLLRDHUP;
                    client_event.data.fd = client_fd;

                    if (epoll_ctl(
                            epoll_fd,
                            EPOLL_CTL_ADD,
                            client_fd,
                            &client_event
                        ) == -1) {
                        std::cerr << "Client registration failed: "
                                  << std::strerror(errno) << '\n';
                        close(client_fd);
                        continue;
                    }

                    clients.try_emplace(client_fd);

                    char ip[INET_ADDRSTRLEN]{};

                    if (inet_ntop(
                            AF_INET,
                            &client_addr.sin_addr,
                            ip,
                            sizeof(ip)
                        ) != nullptr) {
                        std::cout << "Client connected: "
                                  << ip << '\n';
                    }
                }

                continue;
            }

            auto it = clients.find(fd);

            if (it == clients.end()) {
                continue;
            }

            ClientState& client = it->second;

            bool close_connection =
                (flags & (EPOLLERR | EPOLLHUP)) != 0;

            // Read until EAGAIN, EOF, or an error.
            // Also drain pending data on a half-close event.
            if (!close_connection &&
                !client.response_ready &&
                (flags & (EPOLLIN | EPOLLRDHUP))) {

                char buffer[1024];

                while (true) {
                    const ssize_t received = recv(
                        fd,
                        buffer,
                        sizeof(buffer),
                        0
                    );

                    if (received > 0) {
                        const auto chunk_size =
                            static_cast<std::size_t>(received);

                        if (client.input.size() > MAX_INPUT_BYTES ||
                            chunk_size >
                                MAX_INPUT_BYTES - client.input.size()) {

                            std::cerr
                                << "Client exceeded input size limit\n";

                            prepareResponse(
                                client,
                                413,
                                "Content Too Large",
                                "CloudShield: request too large.\n"
                            );

                            break;
                        }

                        client.input.append(
                            buffer,
                            chunk_size
                        );

                        // Parse after every received chunk.
                        processHttpRequest(client);

                        if (client.response_ready) {
                            break;
                        }

                        continue;
                    }

                    if (received == 0) {
                        client.read_closed = true;
                        std::cout << "Client finished sending\n";
                        break;
                    }

                    if (errno == EINTR) {
                        continue;
                    }

                    if (errno == EAGAIN ||
                        errno == EWOULDBLOCK) {
                        break;
                    }

                    std::cerr << "recv() failed: "
                              << std::strerror(errno) << '\n';

                    close_connection = true;
                    break;
                }

                // Re-evaluate an incomplete request after EOF.
                if (!close_connection &&
                    client.read_closed &&
                    !client.response_ready) {

                    if (client.input.empty()) {
                        close_connection = true;
                    } else {
                        processHttpRequest(client);
                    }
                }
            }

            // Support partial writes on non-blocking sockets.
            if (!close_connection &&
                client.sent < client.output.size()) {

                while (client.sent < client.output.size()) {
                    const ssize_t sent = send(
                        fd,
                        client.output.data() + client.sent,
                        client.output.size() - client.sent,
                        MSG_NOSIGNAL
                    );

                    if (sent > 0) {
                        client.sent +=
                            static_cast<std::size_t>(sent);
                        continue;
                    }

                    if (sent == -1 && errno == EINTR) {
                        continue;
                    }

                    if (sent == -1 &&
                        (errno == EAGAIN ||
                         errno == EWOULDBLOCK)) {
                        break;
                    }

                    std::cerr << "send() failed: "
                              << std::strerror(errno) << '\n';

                    close_connection = true;
                    break;
                }
            }

            // Every generated response uses Connection: close.
            if (!close_connection &&
                client.response_ready &&
                client.sent == client.output.size()) {
                close_connection = true;
            }

            if (!close_connection &&
                !updateInterest(epoll_fd, fd, client)) {
                std::cerr << "epoll_ctl(MOD) failed: "
                          << std::strerror(errno) << '\n';
                close_connection = true;
            }

            if (close_connection) {
                removeClient(epoll_fd, fd, clients);
            }
        }
    }

shutdown:
    for (const auto& [fd, state] : clients) {
        (void)state;
        close(fd);
    }

    close(epoll_fd);
    close(server_fd);

    return 0;
}
