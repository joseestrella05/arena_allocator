#pragma once

#include <string_view>
#include <stdexcept>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

class FastFileReader {
private:
    int fd_{-1};
    size_t file_size_{0};
    const char* mapped_data_{nullptr};

public:
    explicit FastFileReader(const std::string& filepath) {
        fd_ = open(filepath.c_str(), O_RDONLY);
        if (fd_ == -1) {
            throw std::runtime_error("No se pudo abrir el archivo de datos: " + filepath);
        }

        struct stat sb;
        if (fstat(fd_, &sb) == -1) {
            close(fd_);
            throw std::runtime_error("Error al obtener tamano del archivo");
        }
        file_size_ = static_cast<size_t>(sb.st_size);

        void* addr = mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (addr == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Fallo al ejecutar mmap en el archivo");
        }
        mapped_data_ = static_cast<const char*>(addr);

        madvise(addr, file_size_, MADV_SEQUENTIAL);
    }

    ~FastFileReader() {
        if (mapped_data_ && mapped_data_ != MAP_FAILED) {
            munmap(const_cast<char*>(mapped_data_), file_size_);
        }
        if (fd_ != -1) {
            close(fd_);
        }
    }

    FastFileReader(const FastFileReader&) = delete;
    FastFileReader& operator=(const FastFileReader&) = delete;

    [[nodiscard]] const char* data() const noexcept { return mapped_data_; }
    [[nodiscard]] size_t size() const noexcept { return file_size_; }
};
