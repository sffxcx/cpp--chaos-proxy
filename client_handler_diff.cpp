--- client_handler.cpp (原始)


+++ client_handler.cpp (修改后)
#include "client_handler.h"
#include "http_parser.h"
#include "chaos_engine.h"
#include <iostream>
#include <thread>
#include <mutex>

// Подключаем заголовочные файлы для работы с сокетами и DNS
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>      // Для getaddrinfo (DNS-резолвинг)
    #include <unistd.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

// Мьютекс для синхронизации вывода в консоль
// Зачем он нужен: когда несколько потоков одновременно пишут в std::cout,
// их вывод может перемешаться. Мьютекс гарантирует, что только один поток
// пишет в консоль в каждый момент времени.
static std::mutex g_log_mutex;

// Макрос для безопасного логирования из разных потоков
#define LOG(msg) { \
    std::lock_guard<std::mutex> lock(g_log_mutex); \
    std::cout << msg << std::endl; \
}

ClientHandler::ClientHandler(int client_socket)
    : client_socket_(client_socket) {
}

ClientHandler::~ClientHandler() {
    // Закрываем сокет клиента при уничтожении
    #ifdef _WIN32
        if (client_socket_ != INVALID_SOCKET) closesocket(client_socket_);
    #else
        if (client_socket_ != INVALID_SOCKET) close(client_socket_);
    #endif
}

void ClientHandler::handle() {
    try {
        // 1. Читаем запрос от клиента
        std::string raw_request = readFromSocket(client_socket_);

        if (raw_request.empty()) {
            LOG("[Handler] Empty request from client");
            return;
        }

        // 2. Парсим запрос
        auto request_opt = parseRequest(raw_request);
        if (!request_opt.has_value()) {
            LOG("[Handler] Failed to parse HTTP request");
            return;
        }

        HttpRequest request = request_opt.value();

        LOG("[Handler] Received " << request.method << " request for " << request.path);

        // 3. Определяем целевой хост
        // Формат запроса к прокси: GET http://target-host.com/path или GET /target-host.com/path
        // Если path начинается с http:// или https://, извлекаем хост из URL
        // Иначе считаем что первый сегмент пути - это хост
        std::string target_host;

        if (request.path.find("http://") == 0 || request.path.find("https://") == 0) {
            // Полный URL, извлекаем хост
            size_t protocol_end = request.path.find("://");
            size_t host_start = protocol_end + 3;
            size_t host_end = request.path.find('/', host_start);
            if (host_end == std::string::npos) {
                host_end = request.path.find('?', host_start);
            }
            if (host_end == std::string::npos) {
                target_host = request.path.substr(host_start);
            } else {
                target_host = request.path.substr(host_start, host_end - host_start);
            }
            // Обновляем путь для пересылки
            if (host_end != std::string::npos) {
                request.path = request.path.substr(host_end);
            } else {
                request.path = "/";
            }
        } else if (!request.path.empty() && request.path[0] == '/') {
            // Путь вида /httpbin.org/get - первый сегмент это хост
            size_t first_slash = 0;
            size_t second_slash = request.path.find('/', 1);
            if (second_slash == std::string::npos) {
                // Нет второго слэша, возможно есть query параметры
                size_t query_pos = request.path.find('?');
                if (query_pos != std::string::npos) {
                    target_host = request.path.substr(1, query_pos - 1);
                    // Query параметры уже распарсены, оставляем путь как /
                } else {
                    target_host = request.path.substr(1);
                }
                request.path = "/";
            } else {
                target_host = request.path.substr(1, second_slash - 1);
                request.path = request.path.substr(second_slash);
            }
        } else {
            // Неизвестный формат
            target_host = request.host;
        }

        // Удаляем порт из хоста если есть
        size_t colon_pos = target_host.find(':');
        if (colon_pos != std::string::npos) {
            target_host = target_host.substr(0, colon_pos);
        }

        if (target_host.empty()) {
            LOG("[Handler] Could not determine target host");
            return;
        }

        LOG("[Handler] Target host: " << target_host << ", path: " << request.path);

        // 4. Применяем задержку (если указана в параметрах)
        applyDelay(request.query_params);

        // 5. Проверяем, нужно ли вернуть ошибку
        if (shouldInjectError(request.query_params)) {
            LOG("[Chaos] Injecting error (503)");
            std::string error_response = generateErrorResponse();
            writeToSocket(client_socket_, error_response);
            return;
        }

        // 6. Соединяемся с целевым сервером
        int remote_socket = connectToHost(target_host);
        if (remote_socket == INVALID_SOCKET) {
            LOG("[Handler] Failed to connect to " << target_host);

            HttpResponse error_resp;
            error_resp.status_code = 502;
            error_resp.status_text = "Bad Gateway";
            error_resp.body = "<html><body><h1>502 Bad Gateway</h1>"
                             "<p>Failed to connect to upstream server: " + target_host + "</p></body></html>";
            writeToSocket(client_socket_, serializeResponse(error_resp));
            return;
        }

        // 7. Сериализуем и отправляем запрос на целевой сервер
        std::string serialized_request = serializeRequest(request, target_host);
        if (!writeToSocket(remote_socket, serialized_request)) {
            LOG("[Handler] Failed to send request to remote server");
            #ifdef _WIN32
                closesocket(remote_socket);
            #else
                close(remote_socket);
            #endif
            return;
        }

        // 8. Получаем ответ от целевого сервера
        std::string raw_response = readFromSocket(remote_socket);

        #ifdef _WIN32
            closesocket(remote_socket);
        #else
            close(remote_socket);
        #endif

        if (raw_response.empty()) {
            LOG("[Handler] Empty response from remote server");
            return;
        }

        // 9. Парсим ответ
        auto response_opt = parseResponse(raw_response);
        if (!response_opt.has_value()) {
            LOG("[Handler] Failed to parse response from remote server");
            writeToSocket(client_socket_, raw_response);
            return;
        }

        HttpResponse response = response_opt.value();

        // 10. Применяем обрезку тела (если указана)
        response.body = truncateBody(response.body, request.query_params);

        response.headers["content-length"] = std::to_string(response.body.size());

        // 11. Отправляем ответ клиенту
        std::string final_response = serializeResponse(response);
        writeToSocket(client_socket_, final_response);

        LOG("[Handler] Response sent to client");

    } catch (const std::exception& e) {
        LOG("[Handler] Exception: " << e.what());
    }
}

std::string ClientHandler::readFromSocket(int socket) {
    std::string result;
    char buffer[4096];

    while (true) {
        // Читаем порцию данных
        int bytes_received = recv(socket, buffer, sizeof(buffer), 0);

        if (bytes_received == SOCKET_ERROR) {
            // Ошибка чтения
            break;
        }

        if (bytes_received == 0) {
            // Соединение закрыто удаленной стороной
            break;
        }

        // Добавляем прочитанное к результату
        result.append(buffer, bytes_received);

        // Если прочитали меньше чем размер буфера, возможно это конец данных
        // Для HTTP это не всегда надежно, но для простоты оставляем так
        if (bytes_received < static_cast<int>(sizeof(buffer))) {
            // Проверяем, есть ли конец заголовков (\r\n\r\n)
            if (result.find("\r\n\r\n") != std::string::npos) {
                // Для HTTP/1.0 или запросов без body этого достаточно
                // Для более надежной работы нужно парсить Content-Length
                // и читать тело соответствующего размера
                break;
            }
        }
    }

    return result;
}

bool ClientHandler::writeToSocket(int socket, const std::string& data) {
    int total_sent = 0;

    while (total_sent < static_cast<int>(data.size())) {
        int bytes_sent = send(socket,
                              data.c_str() + total_sent,
                              static_cast<int>(data.size() - total_sent),
                              0);

        if (bytes_sent == SOCKET_ERROR) {
            return false;
        }

        total_sent += bytes_sent;
    }

    return true;
}

int ClientHandler::connectToHost(const std::string& host, int port) {
    // Используем getaddrinfo для DNS-резолвинга
    // Это современный способ, который поддерживает и IPv4, и IPv6
    struct addrinfo hints{}, *result;
    hints.ai_family = AF_INET;          // Только IPv4 (для простоты)
    hints.ai_socktype = SOCK_STREAM;    // TCP

    std::string port_str = std::to_string(port);

    int ret = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
    if (ret != 0) {
        LOG("[Handler] DNS resolution failed for " << host << ": " << gai_strerror(ret));
        return INVALID_SOCKET;
    }

    // Создаем сокет
    int sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    // Устанавливаем таймаут на подключение (опционально)
    // Для простоты не делаем, но в продакшене это важно

    // Подключаемся
    if (connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) == SOCKET_ERROR) {
        #ifdef _WIN32
            closesocket(sock);
        #else
            close(sock);
        #endif
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    freeaddrinfo(result);
    return sock;
}