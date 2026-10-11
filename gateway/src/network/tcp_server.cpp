
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr int PORT = 8080;
constexpr int BACKLOG = 10;
constexpr int MAX_EVENTS = 64;

bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1) {
        std::cerr << "fcntl(F_GETFL) failed: "
                  << std::strerror(errno) << '\n';
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "fcntl(F_SETFL) failed: "
                  << std::strerror(errno) << '\n';
        return false;
    }

    return true;
}

void closeClient(std::vector<pollfd>& clients, std::size_t index) {
    close(clients[index].fd);
    clients.erase(clients.begin() + static_cast<std::ptrdiff_t>(index));
}

}  // namespace

int runTcpServer() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

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

    if (!setNonBlocking(server_fd)) {
        close(server_fd);
        return 1;
    }

    std::vector<pollfd> clients;
    clients.push_back({server_fd, POLLIN, 0});

    std::cout << "CloudShield non-blocking TCP server on port "
              << PORT << '\n';
    std::cout << "Readiness monitoring: poll()\n";
    std::cout << "Press Ctrl+C to stop.\n";

    while (true) {
        int ready = poll(clients.data(), clients.size(), -1);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "poll() failed: "
                      << std::strerror(errno) << '\n';
            break;
        }

        // Accept connections when the listening socket is ready.
        if (clients[0].revents & POLLIN) {
            while (true) {
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);

                int client_fd = accept(
                    server_fd,
                    reinterpret_cast<sockaddr*>(&client_addr),
                    &client_len
                );

                if (client_fd == -1) {
                    if (errno == EINTR) {
                        continue;
                    }

                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }

                    std::cerr << "accept() failed: "
                              << std::strerror(errno) << '\n';
                    break;
                }

                if (!setNonBlocking(client_fd)) {
                    close(client_fd);
                    continue;
                }

                char ip[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET, &client_addr.sin_addr,
                          ip, sizeof(ip));

                std::cout << "Client connected: " << ip << '\n';

                clients.push_back({client_fd, POLLIN, 0});
            }
        }

        // Process ready client sockets.
        for (std::size_t i = 1; i < clients.size();) {
            short events = clients[i].revents;

            if (events & (POLLERR | POLLNVAL)) {
                closeClient(clients, i);
                continue;
            }

            if (events & (POLLIN | POLLHUP)) {
                char buffer[1024];

                ssize_t received = recv(
                    clients[i].fd, buffer, sizeof(buffer), 0
                );

                if (received > 0) {
                    std::cout << "Received " << received
                              << " bytes from client\n";

                    const char response[] =
                        "CloudShield TCP server received your message.\n";

                    ssize_t sent = send(
                        clients[i].fd,
                        response,
                        sizeof(response) - 1,
                        MSG_NOSIGNAL
                    );

                    if (sent == -1 &&
                        errno != EAGAIN &&
                        errno != EWOULDBLOCK &&
                        errno != EINTR) {
                        std::cerr << "send() failed: "
                                  << std::strerror(errno) << '\n';
                        closeClient(clients, i);
                        continue;
                    }

                    // This learning version expects small test messages.
                    // A production server must queue partial writes.
                    if (sent >= 0 &&
                        static_cast<std::size_t>(sent) != sizeof(response) - 1) {
                        std::cerr << "Partial response; closing client\n";
                        closeClient(clients, i);
                        continue;
                    }
                } else if (received == 0) {
                    closeClient(clients, i);
                    continue;
                } else if (errno != EAGAIN &&
                           errno != EWOULDBLOCK &&
                           errno != EINTR) {
                    std::cerr << "recv() failed: "
                              << std::strerror(errno) << '\n';
                    closeClient(clients, i);
                    continue;
                }
            }

            ++i;
        }
    }

    for (const auto& client : clients) {
        close(client.fd);
    }

    return 0;
}
