
#include "network/tcp_server.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
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
    std::string output;
    std::size_t sent = 0;
    std::size_t received_bytes = 0;
    bool read_closed = false;
};

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

    if (!client.read_closed) {
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

}  // namespace

int runTcpServer() {
    // Create a non-blocking TCP listening socket.
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

    // Allow the port to be reused after the server stops.
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

    // Create the epoll instance.
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd == -1) {
        std::cerr << "epoll_create1() failed: "
                  << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    // Register the listening socket with epoll.
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

    std::cout << "CloudShield TCP server listening on port "
              << PORT << '\n';
    std::cout << "Readiness monitoring: epoll\n";
    std::cout << "Input limit: "
              << MAX_INPUT_BYTES << " bytes per connection\n";
    std::cout << "Output buffering: enabled\n";
    std::cout << "Half-close handling: enabled\n";
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
            int fd = events[i].data.fd;
            std::uint32_t flags = events[i].events;

            // Handle new incoming connections.
            if (fd == server_fd) {
                if (flags & (EPOLLERR | EPOLLHUP)) {
                    std::cerr << "Listening socket error\n";
                    goto shutdown;
                }

                if (!(flags & EPOLLIN)) {
                    continue;
                }

                // Accept connections until the socket would block.
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

            // Receive data until the socket would block or EOF occurs.
            if (!close_connection && (flags & EPOLLIN)) {
                char buffer[1024];

                while (true) {
                    ssize_t received = recv(
                        fd,
                        buffer,
                        sizeof(buffer),
                        0
                    );

                    if (received > 0) {
                        const auto chunk_size =
                            static_cast<std::size_t>(received);

                        // Correctly enforce the limit across all reads.
                        if (client.received_bytes > MAX_INPUT_BYTES ||
                            chunk_size >
                                MAX_INPUT_BYTES - client.received_bytes) {
                            client.output =
                                "CloudShield: input too large.\n";
                            client.sent = 0;
                            client.read_closed = true;

                            std::cerr
                                << "Client exceeded input size limit\n";
                            break;
                        }

                        client.received_bytes += chunk_size;

                        std::cout << "Received " << received
                                  << " bytes from client\n";
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

                // Reply after the client finishes sending.
                // Do not overwrite an oversized-input error response.
                if (!close_connection &&
                    client.read_closed &&
                    client.output.empty()) {
                    client.output =
                        "CloudShield TCP server received your message.\n";
                    client.sent = 0;
                }
            }

            // Send pending bytes, supporting partial writes.
            if (!close_connection &&
                ((flags & EPOLLOUT) ||
                 client.sent < client.output.size())) {
                while (client.sent < client.output.size()) {
                    ssize_t sent = send(
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

            // Close after all pending output has been sent.
            if (!close_connection &&
                client.read_closed &&
                client.sent == client.output.size()) {
                close_connection = true;
            }

            // Update epoll events for the client.
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

    return 1;
}
