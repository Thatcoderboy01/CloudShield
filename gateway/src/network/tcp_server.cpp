
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace {

constexpr int PORT = 8080;
constexpr int BACKLOG = 10;

}  // namespace

int runTcpServer() {
    // 1. Create a TCP socket.
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        std::cerr << "socket() failed: "
                  << std::strerror(errno) << '\n';
        return 1;
    }

    // Allow the port to be reused after restarting the server.
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

    // 3. Bind the socket to port 8080.
    if (bind(server_fd,
             reinterpret_cast<sockaddr*>(&server_addr),
             sizeof(server_addr)) == -1) {
        std::cerr << "bind() failed: "
                  << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    // 4. Start listening for incoming connections.
    if (listen(server_fd, BACKLOG) == -1) {
        std::cerr << "listen() failed: "
                  << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    std::cout << "CloudShield TCP Server listening on port "
              << PORT << '\n';
    std::cout << "Press Ctrl+C to stop.\n";

    // 5. Accept and handle connections sequentially.
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

            std::cerr << "accept() failed: "
                      << std::strerror(errno) << '\n';
            continue;
        }

        char buffer[1024]{};

        ssize_t bytes_received =
            recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';

            std::cout << "Client sent: " << buffer << '\n';

            const char response[] =
                "CloudShield TCP server received your message.\n";

            ssize_t bytes_sent =
                send(client_fd, response, sizeof(response) - 1, 0);

            if (bytes_sent == -1) {
                std::cerr << "send() failed: "
                          << std::strerror(errno) << '\n';
            }
        } else if (bytes_received == -1) {
            std::cerr << "recv() failed: "
                      << std::strerror(errno) << '\n';
        }

        // 6. Close this client's connection.
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
