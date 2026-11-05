#include "common.hpp"
#include <fstream>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

//  отправка полного буфера данных 
bool send_all(int socket, const char* data, size_t length) {
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t sent = send(socket, data + total_sent, length - total_sent, 0);
        if (sent <= 0) return false; // ошибка сети или разрыв соединения
        total_sent += sent;
    }
    return true;
}

//  получение полного буфера данных.....
bool recv_all(int socket, char* data, size_t length) {
    size_t total_recv = 0;
    while (total_recv < length) {
        ssize_t received = recv(socket, data + total_recv, length - total_recv, 0);
        if (received <= 0) return false;
        total_recv += received;
    }
    return true;
}

// функция обработчик сессии клиента (выполняется в отдельном потоке)
void handle_client(int client_fd) {
    // 1 принимаем служебный заголовок
    ControlPacket pkt;
    if (!recv_all(client_fd, reinterpret_cast<char*>(&pkt), sizeof(pkt))) {
        close(client_fd);
        return;
    }

    // 2 читаем путь к файлу
    std::string path(pkt.path_len, '\0');
    if (!recv_all(client_fd, &path[0], pkt.path_len)) {
        close(client_fd);
        return;
    }

    std::vector<char> buffer(pkt.chunk_size);

    // 3 обработка скачивания (клиент запрашивает файл у сервера)
    if (pkt.mode == TransferMode::DOWNLOAD) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[Ошибка] Файл не найден: " << path << "\n";
            close(client_fd);
            return;
        }

        // вычисляем размер файла и сообщаем клиенту
        file.seekg(0, std::ios::end);
        uint64_t actual_size = file.tellg();
        file.seekg(0, std::ios::beg);

        send_all(client_fd, reinterpret_cast<char*>(&actual_size), sizeof(actual_size));

        // передача данных чанками
        while (file) {
            file.read(buffer.data(), pkt.chunk_size);
            std::streamsize bytes_read = file.gcount();
            if (bytes_read <= 0) break;
            if (!send_all(client_fd, buffer.data(), bytes_read)) break;
        }
        std::cout << "[Сервер] Файл успешно отправлен клиенту: " << path << "\n";

    // 4 обработка загрузки (клиент отправляет файл на сервер)
    } else if (pkt.mode == TransferMode::UPLOAD) {
        // атомарная запись во временный файл для защиты от повреждения при сбое
        std::string tmp_path = path + ".tmp";
        std::ofstream file(tmp_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[Ошибка] Не удалось создать временный файл: " << tmp_path << "\n";
            close(client_fd);
            return;
        }

        uint64_t remaining = pkt.file_size;
        bool success = true;

        while (remaining > 0) {
            size_t to_read = std::min<uint64_t>(pkt.chunk_size, remaining);
            if (!recv_all(client_fd, buffer.data(), to_read)) {
                success = false;
                break;
            }
            file.write(buffer.data(), to_read);
            remaining -= to_read;
        }
        file.close();

        // переименовываем временный файл только если успешный прием 100 процентов
        if (success) {
            std::rename(tmp_path.c_str(), path.c_str());
            std::cout << "[Сервер] Файл успешно принят от клиента: " << path << "\n";
        } else {
            std::remove(tmp_path.c_str());
            std::cerr << "[Ошибка] Передача прервана. Временный файл удален.\n";
        }
    }

    close(client_fd);
}

int main(int argc, char* argv[]) {
    AppArgs args;
    parse_args(argc, argv, args, false);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Ошибка создания сокета\n";
        return 1;
    }

    // разрешаем повторное использование порта при быстром перезапуске
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(args.port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Ошибка bind на порту " << args.port << "\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "Ошибка listen\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Многопоточный сервер запущен на порту " << args.port << "...\n";

    // главный цикл приема клиентов
    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) continue;

        // выносим обработку каждого клиента в независимый поток
        std::thread client_thread(handle_client, client_fd);
        client_thread.detach(); // отсоединяем поток чтобы основной цикл сразу вернулся к accept
    }

    close(server_fd);
    return 0;
}