# Используем стабильный образ Ubuntu 22.04
FROM ubuntu:22.04

# Предотвращаем интерактивные запросы при установке пакетов
ENV DEBIAN_FRONTEND=noninteractive

# Устанавливаем компилятор C++, CMake, Make и другие инструменты
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    make \
    g++ \
    && rm -rf /var/lib/apt/lists/*

# Устанавливаем рабочую директорию внутри контейнера
WORKDIR /app

# Копируем все файлы проекта в контейнер
COPY . .

# Создаем папку build и собираем проект через CMake
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make

# Открываем порт 8080
EXPOSE 8080

# Запускаем наш скомпилированный бинарник chaos_proxy на порту 8080
CMD ["./build/chaos_proxy", "8080"]
