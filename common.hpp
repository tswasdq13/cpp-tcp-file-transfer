#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

// режим передачи данных
enum class TransferMode : uint8_t {
    DOWNLOAD = 1, // если 1 - скачивание файла с сервера на клиент
    UPLOAD = 2    // а если 2 - загрузка файла с клиента на сервер
};

// плотная упаковка структуры для ровной передачи по сети
#pragma pack(push, 1)
struct ControlPacket {
    TransferMode mode;     // режим работы (скачать или загрузить)
    uint64_t file_size;    // полный размер файла в байтах
    uint32_t chunk_size;   // размер чанка пернедачи
    uint32_t path_len;     // длина строки удаленного пути к файлу
};
#pragma pack(pop)

// конфигурация параметров приложения
struct AppArgs {
    std::string ip = "127.0.0.1";
    int port = 4040;
    std::string src_path;
    std::string dst_path;
    size_t chunk_size = 1024; // задаем размер чанка по умолчанию
    TransferMode mode = TransferMode::DOWNLOAD;
};


inline bool parse_args(int argc, char* argv[], AppArgs& args, bool is_client = true) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-p" && i + 1 < argc) {
            args.port = std::stoi(argv[++i]);
        } else if (arg == "-chunk" && i + 1 < argc) {
            args.chunk_size = std::stoull(argv[++i]);
        } else if (arg == "-src" && i + 1 < argc) {
            args.src_path = argv[++i];
        } else if (arg == "-dst" && i + 1 < argc) {
            args.dst_path = argv[++i];
        }
    }

    if (!is_client) return true; // серверу потребуется только порт

    if (args.src_path.empty() || args.dst_path.empty()) return false;

    // проверяем есть ли двоеточие чтобы понять качаем или отправляем
    size_t src_colon = args.src_path.find(':');
    size_t dst_colon = args.dst_path.find(':');

    if (src_colon != std::string::npos) {
        args.mode = TransferMode::DOWNLOAD;
        args.ip = args.src_path.substr(0, src_colon);
        args.src_path = args.src_path.substr(src_colon + 1);
    } else if (dst_colon != std::string::npos) {
        args.mode = TransferMode::UPLOAD;
        args.ip = args.dst_path.substr(0, dst_colon);
        args.dst_path = args.dst_path.substr(dst_colon + 1);
    }

    return true;
}