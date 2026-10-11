
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace {

constexpr int PORT = 8080;
constexpr int BACKLOG = 10;
constexpr int MAX_EVENTS = 64;

void removeClient(int epoll_fd, int client_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
}

}  // namespace

int runTcpServer() {
    // 1. Create a non-blocking TCP listening socket.
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
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) == -1) {
        std::cerr << "setsockopt() failed: "
                  << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    // 2. Configure the server address.
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_fd,
             reinterpret_cast<sockaddr*>(&server_addr),
             sizeof(server_addr)) == -1) {
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

    // 3. Create the epoll instance.
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd == -1) {
        std::cerr << "epoll_create1() failed: "
                  << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    // 4. Register the listening socket.
    epoll_event server_event{};
    server_event.events = EPOLLIN;
    server_event.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                  server_fd, &server_event) == -1) {
        std::cerr << "epoll_ctl() failed: "
                  << std::strerror(errno) << '\n';
        close(epoll_fd);
        close(server_fd);
        return 1;
    }

    std::cout << "CloudShield TCP server listening on port "
              << PORT << '\n';
    std::cout << "Readiness monitoring: epoll\n";
    std::cout << "Press Ctrl+C to stop.\n";

    epoll_event events[MAX_EVENTS];

    // 5. Wait for socket events.
    while (true) {
        int count = epoll_wait(
            epoll_fd, events, MAX_EVENTS, -1
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
            uint32_t flags = events[i].events;

            if (flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                if (fd != server_fd) {
                    removeClient(epoll_fd, fd);
                }
                continue;
            }

            // 6. Accept all pending connections.
            if (fd == server_fd) {
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

                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                                  client_fd, &client_event) == -1) {
                        std::cerr << "Client registration failed: "
                                  << std::strerror(errno) << '\n';
                        close(client_fd);
                        continue;
                    }

                    char ip[INET_ADDRSTRLEN]{};
                    inet_ntop(AF_INET, &client_addr.sin_addr,
                              ip, sizeof(ip));

                    std::cout << "Client connected: "
                              << ip << '\n';
                }

                continue;
            }

            // 7. Receive data from a ready client.
            if (flags & EPOLLIN) {
                char buffer[1024];

                ssize_t received = recv(
                    fd, buffer, sizeof(buffer), 0
                );

                if (received > 0) {
                    std::cout << "Received "
                              << received << " bytes\n";

                    const char response[] =
                        "CloudShield TCP server received your message.\n";

                    ssize_t sent = send(
                        fd, response, sizeof(response) - 1,
                        MSG_NOSIGNAL
                    );

                    if (sent == -1) {
                        std::cerr << "send() failed: "
                                  << std::strerror(errno) << '\n';
                    } else if (
                        static_cast<std::size_t>(sent) !=
                        sizeof(response) - 1
                    ) {
                        std::cerr
                            << "Partial response; output buffering "
                            << "is not implemented yet.\n";
                    }

                    // This learning version handles one message
                    // per client connection.
                    removeClient(epoll_fd, fd);
                } else if (received == 0) {
                    removeClient(epoll_fd, fd);
                } else if (
                    errno != EAGAIN &&
                    errno != EWOULDBLOCK &&
                    errno != EINTR
                ) {
                    std::cerr << "recv() failed: "
                              << std::strerror(errno) << '\n';
                    removeClient(epoll_fd, fd);
                }
            }
        }
    }

    close(epoll_fd);
    close(server_fd);
    return 1;
}
