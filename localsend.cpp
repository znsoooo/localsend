#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <filesystem>

#pragma comment(lib, "ws2_32.lib")

#define BUFFER_SIZE 4096
#define SERVER_PORT 9295

namespace fs = std::filesystem;

std::string unique_path(fs::path path) {
    auto root = (path.parent_path() / path.stem()).string();
    auto ext = path.extension().string();
    auto cnt = 2;
    while (fs::exists(path)) {
        path = root + "_" + std::to_string(cnt) + ext;
        cnt++;
    }
    return path.string();
}

class SocketWrapper {
public:
    int sock;
    explicit SocketWrapper(int socket_fd) : sock(socket_fd) {}

    void send_number(size_t number) {
        ssize_t sent = send(sock, (char*)&number, sizeof(number), 0);
        if (sent < 0 || static_cast<size_t>(sent) != sizeof(number)) {
            throw std::runtime_error("Failed to send number");
        }
    }

    size_t recv_number() {
        size_t number;
        ssize_t received = recv(sock, (char*)&number, sizeof(number), 0);
        if (received < 0) {
            throw std::runtime_error("Failed to receive number");
        }
        if (received == 0) {
            throw std::runtime_error("Connection closed by peer while receiving number");
        }
        if (static_cast<size_t>(received) != sizeof(number)) {
            throw std::runtime_error("Incomplete number received");
        }
        return number;
    }

    void send_data(const char* data, size_t length) {
        size_t total_sent = 0;
        while (total_sent < length) {
            ssize_t sent = send(sock, data + total_sent, length - total_sent, 0);
            if (sent < 0) {
                throw std::runtime_error("Failed to send data");
            }
            total_sent += static_cast<size_t>(sent);
        }
    }

    std::vector<char> recv_data(size_t length) {
        std::vector<char> buffer(length);
        size_t total_received = 0;

        while (total_received < length) {
            ssize_t received = recv(sock, buffer.data() + total_received, length - total_received, 0);
            if (received < 0) {
                throw std::runtime_error("Failed to receive data");
            }
            if (received == 0) {
                throw std::runtime_error("Connection closed by peer while receiving data");
            }
            total_received += static_cast<size_t>(received);
        }

        return buffer;
    }

    void send_string(const std::string& data) {
        size_t length = data.length();
        send_number(length);
        if (length > 0) {
            send_data(data.c_str(), length);
        }
    }

    std::string recv_string() {
        size_t length = recv_number();
        if (length == 0) {
            return std::string();
        }
        std::vector<char> buffer = recv_data(length);
        return std::string(buffer.data(), length);
    }

    void send_file(std::ifstream& file) {
        if (!file.is_open()) {
            throw std::runtime_error("File is not open for reading");
        }

        file.seekg(0, std::ios::end);
        size_t file_size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        send_number(file_size);

        std::vector<char> buffer(BUFFER_SIZE);
        while (file.read(buffer.data(), BUFFER_SIZE) || file.gcount() > 0) {
            size_t bytes_to_send = static_cast<size_t>(file.gcount());
            send_data(buffer.data(), bytes_to_send);
        }
    }

    void recv_file(std::ofstream& file) {
        if (!file.is_open()) {
            throw std::runtime_error("File is not open for writing");
        }

        size_t file_size = recv_number();

        std::vector<char> buffer(BUFFER_SIZE);
        size_t bytes_received = 0;

        while (bytes_received < file_size) {
            size_t bytes_to_receive = std::min((size_t)BUFFER_SIZE, file_size - bytes_received);
            std::vector<char> recv_buffer = recv_data(bytes_to_receive);
            file.write(recv_buffer.data(), bytes_to_receive);
            bytes_received += bytes_to_receive;
        }
    }
};

bool send_file(SOCKET sock, const fs::path& file_path) {
    // Open file in binary mode
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << file_path << std::endl;
        return false;
    }

    std::string file_name = file_path.filename().string();

    auto sock_wrap = SocketWrapper(sock);
    sock_wrap.send_string(file_name);
    sock_wrap.send_file(file);

    std::cout << "File sent successfully: " << file_path << std::endl;
    return true;
}

bool receive_file(SOCKET sock) {
    auto sock_wrap = SocketWrapper(sock);

    std::string file_name = sock_wrap.recv_string();
    file_name = unique_path(file_name);
    std::ofstream file(file_name, std::ios::binary);

    // Open file for writing
    if (!file.is_open()) {
        std::cerr << "Failed to create file: " << file_name << std::endl;
        return false;
    }

    sock_wrap.recv_file(file);

    std::cout << "File received successfully: " << file_name << std::endl;
    return true;
}

void server_mode() {
    WSADATA wsa_data;
    SOCKET server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    int client_len = sizeof(client_addr);

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
        return;
    }

    // Create socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) {
        std::cerr << "Failed to create socket" << std::endl;
        WSACleanup();
        return;
    }

    // Set up server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    // Bind socket
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Failed to bind socket" << std::endl;
        closesocket(server_sock);
        WSACleanup();
        return;
    }

    // Listen for connections
    if (listen(server_sock, 1) == SOCKET_ERROR) {
        std::cerr << "Failed to listen on socket" << std::endl;
        closesocket(server_sock);
        WSACleanup();
        return;
    }

    std::cout << "Server listening on port " << SERVER_PORT << std::endl;

    while (true) {
        // Accept connection
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock == INVALID_SOCKET) {
            std::cerr << "Accept failed" << std::endl;
            continue;
        }

        std::cout << "Client connected from " << inet_ntoa(client_addr.sin_addr) << std::endl;

        // Handle client
        receive_file(client_sock);

        closesocket(client_sock);
    }

    closesocket(server_sock);
    WSACleanup();
}

void client_mode(const std::string& server_ip, const std::vector<std::string>& file_paths) {
    WSADATA wsa_data;
    SOCKET client_sock;
    struct sockaddr_in server_addr;
    int result;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
        return;
    }

    // Create socket
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        WSACleanup();
        return;
    }

    // Set up server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    // Convert IP address string to binary form
    // Use inet_addr instead of inet_pton for better Windows compatibility
    server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        std::cerr << "Invalid IP address: " << server_ip << std::endl;
        closesocket(client_sock);
        WSACleanup();
        return;
    }

    // Connect to server
    std::cout << "Connecting to server at " << server_ip << ":" << SERVER_PORT << std::endl;
    result = connect(client_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result == SOCKET_ERROR) {
        std::cerr << "Connection failed" << std::endl;
        closesocket(client_sock);
        WSACleanup();
        return;
    }

    std::cout << "Connected to server successfully" << std::endl;

    // Send files
    for (const std::string& file_path : file_paths) {
        if (!send_file(client_sock, file_path)) {
            std::cerr << "Failed to send file: " << file_path << std::endl;
        }
    }

    closesocket(client_sock);
    WSACleanup();
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        // Server mode
        std::cout << "Starting server mode..." << std::endl;
        server_mode();
    } else if (argc > 2) {
        // Client mode
        std::string server_ip = argv[1];
        std::vector<std::string> file_paths;
        for (int i = 2; i < argc; i++) {
            file_paths.push_back(argv[i]);
        }
        std::cout << "Starting client mode..." << std::endl;
        std::cout << "Connecting to server at " << server_ip << std::endl;
        client_mode(server_ip, file_paths);
    } else {
        std::cout << "Usage:" << std::endl;
        std::cout << "  localsend.exe                    - Start server mode" << std::endl;
        std::cout << "  localsend.exe IP PATH1 PATH2 ... - Send files to server" << std::endl;
    }

    return 0;
}
