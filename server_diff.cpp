--- server.cpp (原始)


+++ server.cpp (修改后)
#include "server.h"
#include <iostream>
#include <functional>
#include <cstring>

// Подключаем заголовочные файлы для работы с сокетами
// Условная компиляция для Windows и Linux/Mac
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")  // Автоматически линкуем библиотеку сокетов на Windows
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

TcpServer::TcpServer(int port)
    : server_socket_(INVALID_SOCKET), port_(port), running_(false) {

    // Инициализация Winsock для Windows
    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    #endif

    createSocket();
}

TcpServer::~TcpServer() {
    // Закрываем сокет при уничтожении объекта
    stop();

    // Завершаем работу Winsock для Windows
    #ifdef _WIN32
        WSACleanup();
    #endif
}

void TcpServer::createSocket() {
    // Создаем TCP-сокет (IPv4)
    // AF_INET - адресное семейство IPv4
    // SOCK_STREAM - потоковый сокет (TCP)
    // 0 - протокол по умолчанию (TCP для SOCK_STREAM)
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket_ == INVALID_SOCKET) {
        throw std::runtime_error("Failed to create socket");
    }

    // Настраиваем опцию SO_REUSEADDR
    // Это позволяет перезапустить сервер сразу после остановки, без ожидания "остывания" порта
    int opt = 1;
    if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) == SOCKET_ERROR) {
        #ifdef _WIN32
            closesocket(server_socket_);
        #else
            close(server_socket_);
        #endif
        throw std::runtime_error("Failed to set SO_REUSEADDR");
    }

    // Заполняем структуру адреса сервера
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;      // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // Слушать все интерфейсы (0.0.0.0)
    server_addr.sin_port = htons(port_);   // Порт в network byte order

    // Привязываем сокет к адресу и порту (bind)
    if (bind(server_socket_, reinterpret_cast<sockaddr*>(&server_addr),
             sizeof(server_addr)) == SOCKET_ERROR) {
        #ifdef _WIN32
            closesocket(server_socket_);
        #else
            close(server_socket_);
        #endif
        throw std::runtime_error("Failed to bind socket. Is port " + std::to_string(port_) + " already in use?");
    }

    // Начинаем слушать порт (listen)
    // 128 - максимальная длина очереди ожидающих подключений
    if (listen(server_socket_, 128) == SOCKET_ERROR) {
        #ifdef _WIN32
            closesocket(server_socket_);
        #else
            close(server_socket_);
        #endif
        throw std::runtime_error("Failed to listen on socket");
    }

    std::cout << "[Server] Listening on port " << port_ << "...\n";
}

void TcpServer::run(const std::function<void(int)>& client_handler) {
    running_ = true;

    while (running_) {
        // Принимаем подключение клиента (accept)
        // Этот вызов блокирующий - он ждет пока не появится новый клиент
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server_socket_,
                                   reinterpret_cast<sockaddr*>(&client_addr),
                                   &client_len);

        // Проверяем флаг остановки после accept
        // Это нужно чтобы не запускать обработчик если сервер уже останавливается
        if (!running_) {
            #ifdef _WIN32
                closesocket(client_socket);
            #else
                close(client_socket);
            #endif
            break;
        }

        if (client_socket == INVALID_SOCKET) {
            if (running_) {  // Если ошибка произошла не из-за остановки
                std::cerr << "[Server] Accept failed\n";
            }
            continue;
        }

        // Получаем IP-адрес клиента для логирования
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        std::cout << "[Server] New connection from " << client_ip << "\n";

        // Вызываем обработчик клиента
        // В нашем случае это будет функция, которая запускает отдельный поток
        try {
            client_handler(client_socket);
        } catch (const std::exception& e) {
            std::cerr << "[Server] Handler error: " << e.what() << "\n";
            #ifdef _WIN32
                closesocket(client_socket);
            #else
                close(client_socket);
            #endif
        }
    }
}

void TcpServer::stop() {
    running_ = false;

    if (server_socket_ != INVALID_SOCKET) {
        #ifdef _WIN32
            closesocket(server_socket_);
        #else
            close(server_socket_);
        #endif
        server_socket_ = INVALID_SOCKET;
    }
}

bool TcpServer::isRunning() const {
    return running_;
}