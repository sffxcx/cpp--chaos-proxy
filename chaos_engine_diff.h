chaos_engine.h 
#ifndef CHAOS_PROXY_CHAOS_ENGINE_H
#define CHAOS_PROXY_CHAOS_ENGINE_H

#include <string>
#include <unordered_map>
#include <random>

/**
 * Модуль "Хаос-движок" отвечает за эмуляцию сбоев сети.
 *
 * Этот модуль читает query-параметры из запроса и применяет соответствующие эффекты:
 * - delay: искусственная задержка перед ответом
 * - error_rate: вероятность возврата ошибки вместо реального ответа
 * - truncate: обрезка тела ответа
 *
 * Почему мы используем отдельный модуль?
 * Это позволяет легко тестировать логику сбоев отдельно от сетевой логики,
 * а также добавлять новые виды сбоев в будущем (например, потерю пакетов).
 */

/**
 * Применяет задержку, указанную в параметрах запроса.
 *
 * @param query_params Параметры из URL (?delay=1000&...)
 *
 * Если параметр delay присутствует, функция блокирует поток на указанное количество миллисекунд.
 * Пример: ?delay=2000 -> задержка 2 секунды.
 */
void applyDelay(const std::unordered_map<std::string, std::string>& query_params);

/**
 * Определяет, нужно ли вернуть ошибку вместо реального ответа.
 *
 * @param query_params Параметры из URL
 * @return true если нужно сгенерировать ошибку, false если продолжить нормально.
 *
 * Параметр error_rate задает вероятность от 0.0 до 1.0.
 * Пример: ?error_rate=0.5 -> 50% шанс получить ошибку.
 *
 * Как это работает:
 * 1. Генерируем случайное число от 0.0 до 1.0.
 * 2. Если число меньше error_rate -> возвращаем true (ошибка).
 */
bool shouldInjectError(const std::unordered_map<std::string, std::string>& query_params);

/**
 * Обрезает тело ответа на 50%, если указан параметр truncate=true.
 *
 * @param body Тело ответа от удаленного сервера.
 * @param query_params Параметры из URL
 * @return Обрезанное тело (или оригинальное, если truncate не указан).
 */
std::string truncateBody(const std::string& body, const std::unordered_map<std::string, std::string>& query_params);

/**
 * Генерирует стандартный HTTP-ответ с ошибкой 503 Service Unavailable.
 * Используется когда срабатывает error_rate.
 */
std::string generateErrorResponse();

#endif // CHAOS_PROXY_CHAOS_ENGINE_H
