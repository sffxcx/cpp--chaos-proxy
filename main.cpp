#include <iostream>
#include <thread>
#include <string>
#include <cstdlib>

#include "server.h"
#include "client_handler.h"

/**
 * Точка входа в приложение Chaos Proxy.
 *
 * Этот файл минималистичен по дизайну:
 * - Парсит аргументы командной строки
 * - Создает сервер
 * - Запускает цикл обработки подключений
 *
 * Вся сложная логика вынесена в другие модули.
 */

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <port>\n";
    std::cout << "Example: " << program_name << " 8080\n";
    std::cout << "\nChaos Proxy - HTTP-прокси с эмуляцией сбоев.\n";
    std::cout << "\nQuery parameters для эмуляции сбоев:\n";
    std::cout << "  ?delay=1000       - задержка ответа на 1000 мс\n";
    std::cout << "  ?error_rate=0.5   - 50% шанс вернуть ошибку 503\n";
    std::cout << "  ?truncate=true    - обрезать тело ответа на 50%\n";
    std::cout << "\nПример использования:\n";
    std::cout << "  curl \"http://localhost:8080/httpbin.org/get?delay=2000&error_rate=0.5\"\n";
}

int main(int argc, char* argv[]) {
    // Проверяем аргументы командной строки
    if (argc != 2) {
        printUsage(argv[0]);
        return 1;
    }

    // Парсим порт из аргументов
    int port;
    try {
        port = std::stoi(argv[1]);

        // Проверяем диапазон портов (1-65535)
        if (port < 1 || port > 65535) {
            std::cerr << "Error: Port must be between 1 and 65535\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: Invalid port number\n";
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "=== Chaos Proxy ===\n";
    std::cout << "Starting server on port " << port << "...\n\n";

    try {
        // Создаем сервер на указанном порту
        TcpServer server(port);

        // Запускаем сервер
        // Для каждого нового подключения создаем отдельный поток
        server.run([](int client_socket) {
            // Лямбда-функция обработчика подключений
            // Она создает новый поток для каждого клиента

            // Запускаем обработку клиента в отдельном потоке
            // detach() означает, что поток будет работать самостоятельно
            // и мы не будем ждать его завершения
            std::thread([client_socket]() {
                ClientHandler handler(client_socket);
                handler.handle();
            }).detach();
        });

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
