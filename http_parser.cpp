#include "http_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

// Вспомогательная функция для приведения строки к нижнему регистру
static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Вспомогательная функция для удаления пробелов по краям строки (trim)
static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::optional<HttpRequest> parseRequest(const std::string& raw_request) {
    HttpRequest request;
    std::istringstream stream(raw_request);
    std::string line;

    // 1. Читаем первую строку: "GET /path HTTP/1.1"
    if (!std::getline(stream, line)) {
        return std::nullopt;
    }

    // Удаляем символы возврата каретки (\r)
    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

    std::istringstream first_line_stream(line);
    if (!(first_line_stream >> request.method >> request.path)) {
        return std::nullopt;
    }

    // 2. Разбираем путь на собственно путь и query-параметры
    size_t query_pos = request.path.find('?');
    if (query_pos != std::string::npos) {
        std::string path_part = request.path.substr(0, query_pos);
        std::string query_part = request.path.substr(query_pos + 1);
        request.path = path_part;

        // Парсим параметры вида key=value&key2=value2
        std::istringstream query_stream(query_part);
        std::string param;
        while (std::getline(query_stream, param, '&')) {
            size_t eq_pos = param.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = param.substr(0, eq_pos);
                std::string value = param.substr(eq_pos + 1);
                request.query_params[key] = value;
            }
        }
    }

    // 3. Читаем заголовки до пустой строки
    while (std::getline(stream, line)) {
        // Удаляем \r
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

        // Пустая строка означает конец заголовков
        if (line.empty()) {
            break;
        }

        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = trim(line.substr(0, colon_pos));
            std::string value = trim(line.substr(colon_pos + 1));

            // Сохраняем ключ в нижнем регистре для удобного поиска
            std::string key_lower = toLower(key);
            request.headers[key_lower] = value;

            // Особое внимание заголовку Host
            if (key_lower == "host") {
                request.host = value;
            }
        }
    }

    // 4. Читаем тело запроса (если есть)
    // Тело читается до конца потока
    std::ostringstream body_stream;
    body_stream << stream.rdbuf();
    request.body = body_stream.str();

    return request;
}

std::optional<HttpResponse> parseResponse(const std::string& raw_response) {
    HttpResponse response;
    std::istringstream stream(raw_response);
    std::string line;

    // 1. Читаем статусную строку: "HTTP/1.1 200 OK"
    if (!std::getline(stream, line)) {
        return std::nullopt;
    }

    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

    std::istringstream status_stream(line);
    std::string http_version;
    if (!(status_stream >> http_version >> response.status_code)) {
        return std::nullopt;
    }

    // Читаем остальную часть строки как текст статуса
    std::getline(status_stream, response.status_text);
    response.status_text = trim(response.status_text);

    // 2. Читаем заголовки
    while (std::getline(stream, line)) {
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

        if (line.empty()) {
            break;
        }

        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = trim(line.substr(0, colon_pos));
            std::string value = trim(line.substr(colon_pos + 1));
            std::string key_lower = toLower(key);
            response.headers[key_lower] = value;
        }
    }

    // 3. Читаем тело ответа
    std::ostringstream body_stream;
    body_stream << stream.rdbuf();
    response.body = body_stream.str();

    return response;
}

std::string serializeRequest(const HttpRequest& request, const std::string& target_host) {
    std::ostringstream oss;

    // Первая строка: МЕТОД /путь HTTP/1.1
    oss << request.method << " " << request.path << " HTTP/1.1\r\n";

    // Заголовки
    // Важно: мы не включаем наши служебные query-параметры, они уже отделены при парсинге
    for (const auto& [key, value] : request.headers) {
        // Не дублируем Host, если он уже есть
        if (key == "host") {
            continue;
        }
        oss << key << ": " << value << "\r\n";
    }

    // Добавляем актуальный Host (целевой сервер)
    oss << "Host: " << target_host << "\r\n";

    // Connection: close - закрываем соединение после ответа
    oss << "Connection: close\r\n";

    oss << "\r\n";

    // Тело запроса (если есть)
    if (!request.body.empty()) {
        oss << request.body;
    }

    return oss.str();
}

std::string serializeResponse(const HttpResponse& response) {
    std::ostringstream oss;

    // Статусная строка
    oss << "HTTP/1.1 " << response.status_code << " " << response.status_text << "\r\n";

    // Заголовки
    for (const auto& [key, value] : response.headers) {
        oss << key << ": " << value << "\r\n";
    }

    // Добавляем Content-Length для тела
    oss << "Content-Length: " << response.body.size() << "\r\n";
    oss << "Connection: close\r\n";

    oss << "\r\n";

    // Тело ответа
    oss << response.body;

    return oss.str();
}

std::string getHeader(const std::unordered_map<std::string, std::string>& headers, const std::string& name) {
    std::string name_lower = toLower(name);
    auto it = headers.find(name_lower);
    if (it != headers.end()) {
        return it->second;
    }
    return "";
}
