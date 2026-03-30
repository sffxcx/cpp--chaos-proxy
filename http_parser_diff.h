--- http_parser.h (原始)


+++ http_parser.h (修改后)
#ifndef CHAOS_PROXY_HTTP_PARSER_H
#define CHAOS_PROXY_HTTP_PARSER_H

#include <string>
#include <unordered_map>
#include <optional>

/**
 * Структура для хранения разобранного HTTP-запроса.
 * Содержит только базовые поля, необходимые для прокси.
 */
struct HttpRequest {
    std::string method;          // Метод: GET, POST и т.д.
    std::string path;            // Путь к ресурсу (например, /get)
    std::string host;            // Хост из заголовка Host
    std::unordered_map<std::string, std::string> headers; // Все заголовки
    std::string body;            // Тело запроса (если есть)
    std::unordered_map<std::string, std::string> query_params; // Параметры из URL (?key=value)
};

/**
 * Структура для хранения разобранного HTTP-ответа.
 */
struct HttpResponse {
    int status_code;             // Код статуса: 200, 404, 503 и т.д.
    std::string status_text;     // Текст статуса: "OK", "Not Found"
    std::unordered_map<std::string, std::string> headers; // Заголовки ответа
    std::string body;            // Тело ответа
};

/**
 * Парсит строку HTTP-запроса в структуру HttpRequest.
 *
 * @param raw_request Сырая строка запроса от клиента.
 * @return HttpRequest если парсинг успешен, иначе std::nullopt.
 *
 * Как это работает:
 * 1. Разделяем первую строку (метод, путь, версия).
 * 2. Извлекаем параметры из пути (все что после '?').
 * 3. Читаем заголовки до пустой строки.
 * 4. Остаток считаем телом запроса.
 */
std::optional<HttpRequest> parseRequest(const std::string& raw_request);

/**
 * Парсит строку HTTP-ответа от удаленного сервера в структуру HttpResponse.
 *
 * @param raw_response Сырая строка ответа от сервера.
 * @return HttpResponse если парсинг успешен, иначе std::nullopt.
 */
std::optional<HttpResponse> parseResponse(const std::string& raw_response);

/**
 * Сериализует структуру HttpRequest обратно в строку для отправки на удаленный сервер.
 * Важно: при пересылке мы убираем наши служебные query-параметры (?delay=..., ?error_rate=...).
 *
 * @param request Структура запроса.
 * @param target_host Хост, куда реально отправляем запрос.
 * @return Строка HTTP-запроса.
 */
std::string serializeRequest(const HttpRequest& request, const std::string& target_host);

/**
 * Сериализует структуру HttpResponse в строку для отправки клиенту.
 *
 * @param response Структура ответа.
 * @return Строка HTTP-ответа.
 */
std::string serializeResponse(const HttpResponse& response);

/**
 * Вспомогательная функция для извлечения значения заголовка по имени.
 * Поиск нечувствителен к регистру (Content-Length == content-length).
 */
std::string getHeader(const std::unordered_map<std::string, std::string>& headers, const std::string& name);

#endif // CHAOS_PROXY_HTTP_PARSER_H