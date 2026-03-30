--- chaos_engine.cpp (原始)


+++ chaos_engine.cpp (修改后)
#include "chaos_engine.h"
#include <chrono>
#include <thread>

void applyDelay(const std::unordered_map<std::string, std::string>& query_params) {
    // Ищем параметр delay в запросе
    auto it = query_params.find("delay");
    if (it == query_params.end()) {
        return; // Задержки нет, ничего не делаем
    }

    // Преобразуем строку в число (миллисекунды)
    try {
        int delay_ms = std::stoi(it->second);
        if (delay_ms > 0) {
            // Логирование для отладки (в реальном проекте лучше использовать логгер)
            // std::cout << "[Chaos] Applying delay: " << delay_ms << "ms\n";

            // Блокируем текущий поток на указанное время
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    } catch (const std::exception& e) {
        // Если параметр некорректный (не число), просто игнорируем его
        // В продакшене здесь было бы логирование ошибки
    }
}

bool shouldInjectError(const std::unordered_map<std::string, std::string>& query_params) {
    // Ищем параметр error_rate
    auto it = query_params.find("error_rate");
    if (it == query_params.end()) {
        return false; // Ошибки нет, продолжаем нормально
    }

    try {
        double error_rate = std::stod(it->second);

        // Ограничиваем вероятность диапазоном [0.0, 1.0]
        if (error_rate < 0.0) error_rate = 0.0;
        if (error_rate > 1.0) error_rate = 1.0;

        // Генератор случайных чисел
        // Важно: создаем генератор каждый раз для простоты
        // В высоконагруженном коде лучше переиспользовать генератор
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<> distrib(0.0, 1.0);

        double random_value = distrib(gen);

        // Если случайное число меньше порога error_rate -> ошибка
        return random_value < error_rate;

    } catch (const std::exception& e) {
        return false; // Некорректный параметр, ошибки не будет
    }
}

std::string truncateBody(const std::string& body, const std::unordered_map<std::string, std::string>& query_params) {
    // Проверяем параметр truncate
    auto it = query_params.find("truncate");
    if (it == query_params.end() || it->second != "true") {
        return body; // Обрезка не включена
    }

    // Обрезаем тело на 50%
    size_t new_length = body.size() / 2;

    // Логирование для отладки
    // std::cout << "[Chaos] Truncating body from " << body.size() << " to " << new_length << " bytes\n";

    return body.substr(0, new_length);
}

std::string generateErrorResponse() {
    // Формируем стандартный HTTP-ответ с ошибкой 503
    std::string body = "<html><body><h1>503 Service Unavailable</h1>"
                       "<p>The service is temporarily unavailable due to chaos injection.</p></body></html>";

    std::string response = "HTTP/1.1 503 Service Unavailable\r\n"
                          "Content-Type: text/html\r\n"
                          "Content-Length: " + std::to_string(body.size()) + "\r\n"
                          "Connection: close\r\n"
                          "\r\n"
                          + body;

    return response;
}