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

#pragma comment(lib, "ws2_32.lib")

#define BUFFER_SIZE 4096
#define SERVER_PORT 9295

using namespace std;

bool send_file(SOCKET sock, const string& file_path) {
    // Open file in binary mode
    ifstream file(file_path, ios::binary);
    if (!file.is_open()) {
        cerr << "Failed to open file: " << file_path << endl;
        return false;
    }

    // Get file size
    file.seekg(0, ios::end);
    long file_size = file.tellg();
    file.seekg(0, ios::beg);

    // Send file name length
    string file_name = file_path.substr(file_path.find_last_of("/\\") + 1);
    int name_length = file_name.length();

    if (send(sock, (char*)&name_length, sizeof(name_length), 0) <= 0) {
        cerr << "Failed to send file name length" << endl;
        file.close();
        return false;
    }

    // Send file name
    if (send(sock, file_name.c_str(), name_length, 0) <= 0) {
        cerr << "Failed to send file name" << endl;
        file.close();
        return false;
    }

    // Send file size
    if (send(sock, (char*)&file_size, sizeof(file_size), 0) <= 0) {
        cerr << "Failed to send file size" << endl;
        file.close();
        return false;
    }

    // Send file data
    char buffer[BUFFER_SIZE];
    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
        int bytes_to_send = file.gcount();
        if (send(sock, buffer, bytes_to_send, 0) <= 0) {
            cerr << "Failed to send file data" << endl;
            file.close();
            return false;
        }
    }

    file.close();
    cout << "File sent successfully: " << file_path << endl;
    return true;
}

bool receive_file(SOCKET sock) {
    // Receive file name length
    int name_length;
    if (recv(sock, (char*)&name_length, sizeof(name_length), 0) <= 0) {
        cerr << "Failed to receive file name length" << endl;
        return false;
    }

    // Receive file name
    char* file_name = new char[name_length + 1];
    if (recv(sock, file_name, name_length, 0) <= 0) {
        cerr << "Failed to receive file name" << endl;
        delete[] file_name;
        return false;
    }
    file_name[name_length] = '\0';

    // Receive file size
    long file_size;
    if (recv(sock, (char*)&file_size, sizeof(file_size), 0) <= 0) {
        cerr << "Failed to receive file size" << endl;
        delete[] file_name;
        return false;
    }

    // Open file for writing
    ofstream file(file_name, ios::binary);
    if (!file.is_open()) {
        cerr << "Failed to create file: " << file_name << endl;
        delete[] file_name;
        return false;
    }

    // Receive file data
    char buffer[BUFFER_SIZE];
    long bytes_received = 0;
    while (bytes_received < file_size) {
        int bytes_to_receive = min(BUFFER_SIZE, static_cast<int>(file_size - bytes_received));
        int received = recv(sock, buffer, bytes_to_receive, 0);
        if (received <= 0) {
            cerr << "Failed to receive file data" << endl;
            file.close();
            delete[] file_name;
            return false;
        }
        file.write(buffer, received);
        bytes_received += received;
    }

    file.close();
    cout << "File received successfully: " << file_name << endl;
    delete[] file_name;
    return true;
}

void server_mode() {
    WSADATA wsa_data;
    SOCKET server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    int client_len = sizeof(client_addr);

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        cerr << "Failed to initialize Winsock" << endl;
        return;
    }

    // Create socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) {
        cerr << "Failed to create socket" << endl;
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
        cerr << "Failed to bind socket" << endl;
        closesocket(server_sock);
        WSACleanup();
        return;
    }

    // Listen for connections
    if (listen(server_sock, 1) == SOCKET_ERROR) {
        cerr << "Failed to listen on socket" << endl;
        closesocket(server_sock);
        WSACleanup();
        return;
    }

    cout << "Server listening on port " << SERVER_PORT << endl;

    while (true) {
        // Accept connection
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock == INVALID_SOCKET) {
            cerr << "Accept failed" << endl;
            continue;
        }

        cout << "Client connected from " << inet_ntoa(client_addr.sin_addr) << endl;

        // Handle client
        receive_file(client_sock);

        closesocket(client_sock);
    }

    closesocket(server_sock);
    WSACleanup();
}

void client_mode(const string& server_ip, const vector<string>& file_paths) {
    WSADATA wsa_data;
    SOCKET client_sock;
    struct sockaddr_in server_addr;
    int result;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        cerr << "Failed to initialize Winsock" << endl;
        return;
    }

    // Create socket
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock == INVALID_SOCKET) {
        cerr << "Socket creation failed" << endl;
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
        cerr << "Invalid IP address: " << server_ip << endl;
        closesocket(client_sock);
        WSACleanup();
        return;
    }

    // Connect to server
    cout << "Connecting to server at " << server_ip << ":" << SERVER_PORT << endl;
    result = connect(client_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result == SOCKET_ERROR) {
        cerr << "Connection failed" << endl;
        closesocket(client_sock);
        WSACleanup();
        return;
    }

    cout << "Connected to server successfully" << endl;

    // Send files
    for (const string& file_path : file_paths) {
        if (!send_file(client_sock, file_path)) {
            cerr << "Failed to send file: " << file_path << endl;
        }
    }

    closesocket(client_sock);
    WSACleanup();
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        // Server mode
        cout << "Starting server mode..." << endl;
        server_mode();
    } else if (argc > 2) {
        // Client mode
        string server_ip = argv[1];
        vector<string> file_paths;
        for (int i = 2; i < argc; i++) {
            file_paths.push_back(argv[i]);
        }
        cout << "Starting client mode..." << endl;
        cout << "Connecting to server at " << server_ip << endl;
        client_mode(server_ip, file_paths);
    } else {
        cout << "Usage:" << endl;
        cout << "  localsend.exe                    - Start server mode" << endl;
        cout << "  localsend.exe IP PATH1 PATH2 ... - Send files to server" << endl;
    }

    return 0;
}
