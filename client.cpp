
#include "common.hpp"
#include <fstream>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

//  отправка полного буфера данных
bool send_all(int socket, const char* data, size_t length) {
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t sent = send(socket, data + total_sent, length - total_sent, 0);
        if (sent <= 0) return false;
        total_sent += sent;
    }
    return true;
}

//  получение полного буфера данных
bool recv_all(int socket, char* data, size_t length) {
    size_t total_recv = 0;
    while (total_recv < length) {
        ssize_t received = recv(socket, data + total_recv, length - total_recv, 0);
        if (received <= 0) return false;
        total_recv += received;
    }
    return true;
}

// вывод процента скачивания в консоль
void print_progress(uint64_t transferred, uint64_t total) {
    double percent = (total > 0) ? (static_cast<double>(transferred) / total * 100.0) : 0.0;
    std::cout << "\rПередано " << transferred << " байт / "
              << "общий размер " << total << " байт "
              << "(" << std::fixed << std::setprecision(1) << percent << "%)" << std::flush;
}

int main(int argc, char* argv[]) {
    AppArgs args;
    if (!parse_args(argc, argv, args, true)) {
        std::cout << "Использование:\n";
        std::cout << "  Скачивание: ./client -p 4040 -chunk 1024 -src 127.0.0.1:file.txt -dst local.txt\n";
        std::cout << "  Отправка:   ./client -p 4040 -chunk 1024 -src local.txt -dst 127.0.0.1:file.txt\n";
        return 1;
    }

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        std::cerr << "Ошибка создания сокета!\n";
        return 1;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(args.port);

    if (inet_pton(AF_INET, args.ip.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Неверный IP адрес: " << args.ip << "\n";
        close(sock_fd);
        return 1;
    }

    if (connect(sock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Ошибка подключения к серверу " << args.ip << ":" << args.port << "\n";
        close(sock_fd);
        return 1;
    }

    // родготовка пакета управления
    ControlPacket pkt{};
    pkt.mode = args.mode;
    pkt.chunk_size = args.chunk_size;

    std::string remote_path = (args.mode == TransferMode::DOWNLOAD) ? args.src_path : args.dst_path;
    std::string local_path = (args.mode == TransferMode::DOWNLOAD) ? args.dst_path : args.src_path;

    pkt.path_len = remote_path.size();

    // В режиме загрузки определяем размер локального файла
    if (args.mode == TransferMode::UPLOAD) {
        std::ifstream file(local_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Ошибка: Локальный файл не найден: " << local_path << "\n";
            close(sock_fd);
            return 1;
        }
        pkt.file_size = file.tellg();
    } else {
        pkt.file_size = 0;
    }

    // отправка управляющего заголовка и пути к файлу
    if (!send_all(sock_fd, reinterpret_cast<char*>(&pkt), sizeof(pkt)) ||
        !send_all(sock_fd, remote_path.c_str(), remote_path.size())) {
        std::cerr << "Ошибка отправки заголовка!\n";
        close(sock_fd);
        return 1;
    }

    std::vector<char> buffer(args.chunk_size);

    // логика скачивания....
    if (args.mode == TransferMode::DOWNLOAD) {
        uint64_t file_size = 0;
        if (!recv_all(sock_fd, reinterpret_cast<char*>(&file_size), sizeof(file_size))) {
            std::cerr << "Ошибка получения размера файла!\n";
            close(sock_fd);
            return 1;
        }

        std::string tmp_local_path = local_path + ".tmp";
        std::ofstream file(tmp_local_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Не удалось создать файл: " << tmp_local_path << "\n";
            close(sock_fd);
            return 1;
        }

        uint64_t transferred = 0;
        bool success = true;

        while (transferred < file_size) {
            size_t to_read = std::min<uint64_t>(args.chunk_size, file_size - transferred);
            if (!recv_all(sock_fd, buffer.data(), to_read)) {
                success = false;
                break;
            }
            file.write(buffer.data(), to_read);
            transferred += to_read;
            print_progress(transferred, file_size);
        }
        file.close();
        std::cout << "\n";

        if (success) {
            std::rename(tmp_local_path.c_str(), local_path.c_str());
            std::cout << "Файл успешно сохранен: " << local_path << "\n";
        } else {
            std::remove(tmp_local_path.c_str());
            std::cerr << "Ошибка скачивания. Временный файл удален.\n";
        }

    // логика отправки....
    } else if (args.mode == TransferMode::UPLOAD) {
        std::ifstream file(local_path, std::ios::binary);
        uint64_t transferred = 0;
        uint64_t total = pkt.file_size;

        while (file) {
            file.read(buffer.data(), args.chunk_size);
            std::streamsize bytes_read = file.gcount();
            if (bytes_read <= 0) break;
            if (!send_all(sock_fd, buffer.data(), bytes_read)) break;

            transferred += bytes_read;
            print_progress(transferred, total);
        }
        std::cout << "\nФайл успешно отправлен на сервер!\n";
    }

    close(sock_fd);
    return 0;
}