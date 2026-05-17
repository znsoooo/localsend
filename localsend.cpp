#include <iostream>
#include <vector>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cmath>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>

    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close

    #define WSAStartup(a, b) 0
    #define WSACleanup()
    #define WSAGetLastError() errno
    typedef struct {} WSADATA;
#endif

#define BUFFER_SIZE 4096
#define SERVER_PORT 9295

namespace fs = std::filesystem;

enum {
    flag_start,
    flag_end,
    flag_root,
    flag_file,
    flag_directory,
    flag_message,
};

void backspace(int n) {
    if (n > 0) {
        std::cout << std::string(n, '\b') << std::string(n, ' ') << std::string(n, '\b');
    }
}

std::string MB(uint64_t size) {
    double mb = size / 1024.0 / 1024.0;
    mb = std::ceil(mb * 10.0) / 10.0;
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << mb;
    return ss.str();
}

void print_progress(uint64_t total_size, uint64_t received_size) {
    static auto last_time = std::chrono::steady_clock::now();
    static auto last_width = 0;

    auto current_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_time).count();

    if (received_size == 0 || duration >= 200 || received_size >= total_size) {
        if (received_size == 0 && total_size > 0) {
            last_width = 0;
        } else if (received_size < total_size) {
            backspace(last_width);
            std::string progress = " (" + MB(received_size) + "/" + MB(total_size) + " MB)";
            std::cout << progress;
            std::cout.flush();
            last_width = progress.size();
        } else {
            backspace(last_width);
            std::cout << " (" << MB(total_size) << " MB)" << std::endl;
            last_width = 0;
        }
        last_time = current_time;
    }
}

std::string ansi_to_utf8(const std::string& text) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, NULL, 0);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, &wstr[0], wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string result(ulen, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], ulen, NULL, NULL);
    result.resize(ulen - 1); // Remove the trailing \0
    return result;
#else
    return text;
#endif
}

std::string utf8_to_ansi(const std::string& text) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wstr[0], wlen);
    int alen = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string result(alen, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &result[0], alen, NULL, NULL);
    result.resize(alen - 1); // Remove the trailing \0
    return result;
#else
    return text;
#endif
}

std::string format_path(fs::path path) {
    if (fs::is_directory(path)) {
        path /= "";
    }
    path = path.lexically_normal().generic_string();
    std::string result = "\"" + path.string() + "\"";
    return utf8_to_ansi(result);
}

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

    void send_data(const void* data, size_t length) {
        size_t total_sent = 0;
        while (total_sent < length) {
            ssize_t sent = send(sock, (char*)data + total_sent, length - total_sent, 0);
            if (sent < 0) {
                throw std::runtime_error("Failed to send data");
            }
            total_sent += sent;
        }
    }

    void recv_data(void* data, size_t length) {
        size_t total_received = 0;
        while (total_received < length) {
            ssize_t received = recv(sock, (char*)data + total_received, length - total_received, 0);
            if (received < 0) {
                throw std::runtime_error("Failed to receive data");
            }
            if (received == 0) {
                throw std::runtime_error("Connection closed by peer while receiving data");
            }
            total_received += received;
        }
    }

    void send_number(size_t number) {
        send_data(&number, sizeof(number));
    }

    size_t recv_number() {
        size_t number;
        recv_data(&number, sizeof(number));
        return number;
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
        std::vector<char> buffer(length);
        recv_data(buffer.data(), length);
        return std::string(buffer.data(), length);
    }

    void send_file(const fs::path& path) {
        std::cout << "Send file: " << format_path(path);
        std::cout.flush();

        std::ifstream file(path, std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("File is not open for reading");
        }

        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        send_number(file_size);
        file.seekg(0, std::ios::beg);

        char buffer[BUFFER_SIZE];
        size_t bytes_received = 0;
        print_progress(file_size, bytes_received);

        while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
            size_t bytes_to_send = file.gcount();
            send_data(buffer, bytes_to_send);
            bytes_received += bytes_to_send;
            print_progress(file_size, bytes_received);
        }

        file.close();
    }

    void recv_file(const fs::path& path) {
        std::cout << "Recv file: " << format_path(path);
        std::cout.flush();

        std::ofstream file(path, std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("File is not open for writing");
        }

        size_t file_size = recv_number();

        char buffer[BUFFER_SIZE];
        size_t bytes_received = 0;
        print_progress(file_size, bytes_received);

        while (bytes_received < file_size) {
            size_t bytes_to_receive = std::min((size_t)BUFFER_SIZE, file_size - bytes_received);
            recv_data(buffer, bytes_to_receive);
            file.write(buffer, bytes_to_receive);
            bytes_received += bytes_to_receive;
            print_progress(file_size, bytes_received);
        }

        file.close();
    }
};

bool send_file(SocketWrapper sock, const fs::path& root_path, const fs::path& send_path) {
    fs::path rel_path = fs::relative(send_path, root_path);
    try {
        if (fs::is_directory(send_path)) {
            sock.send_number(flag_directory);
            sock.send_string(rel_path.string());
            std::cout << "Send file: " << format_path(send_path) << std::endl;
        } else {
            sock.send_number(flag_file);
            sock.send_string(rel_path.string());
            sock.send_file(send_path);
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool send_files(SocketWrapper sock, const std::vector<std::string>& send_paths) {
    for (const fs::path& send_path : send_paths) {
        sock.send_number(flag_root);
        sock.send_string(send_path.filename().string());
        if (fs::exists(send_path)) {
            send_file(sock, send_path, send_path);
            if (fs::is_directory(send_path)) {
                for (const auto& entry : fs::recursive_directory_iterator(send_path)) {
                    send_file(sock, send_path, entry);
                }
            }
        }
    }
    sock.send_number(flag_end);
    return true;
}

bool receive_files(SocketWrapper sock) {
    fs::path root_path, rel_path, full_path;

    while (true) {
        int flag = sock.recv_number();

        switch (flag) {
            case flag_root:
                root_path = sock.recv_string();
                root_path = unique_path(root_path);
                break;

            case flag_directory:
                rel_path = sock.recv_string();
                full_path = root_path / rel_path;
                fs::create_directories(full_path);
                std::cout << "Recv file: " << format_path(full_path) << std::endl;
                break;

            case flag_file:
                rel_path = sock.recv_string();
                full_path = rel_path == "." ? root_path : root_path / rel_path;
                if (full_path.parent_path() != "") {
                    fs::create_directories(full_path.parent_path());
                }
                sock.recv_file(full_path);
                break;

            case flag_message:
                break;

            case flag_end:
                return true;

            default:
                throw std::runtime_error("Unknown flag received: " + std::to_string(flag));
        }
    }

    return false;
}

void server_mode() {
    WSADATA wsa_data;
    SOCKET server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

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
        auto client_sock_wrap = SocketWrapper(client_sock);
        try {
            receive_files(client_sock_wrap);
        } catch (...) {
            std::cerr << std::endl << "Error occurred while receiving files" << std::endl;
        }

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
    auto client_sock_wrap = SocketWrapper(client_sock);
    try {
        send_files(client_sock_wrap, file_paths);
    } catch (...) {
        std::cerr << std::endl << "Error occurred while sending files" << std::endl;
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
            file_paths.push_back(ansi_to_utf8(argv[i]));
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
