#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>
#include <stdexcept>

class ArenaAllocator {
private:
    std::byte* buffer_{nullptr};
    std::size_t capacity_{0};
    std::size_t offset_{0};

    // Función auxiliar: alinear una dirección hacia arriba
    static std::uintptr_t align_forward(std::uintptr_t ptr, std::size_t alignment) {
        return (ptr + (alignment - 1)) & ~(alignment - 1);
    }

public:
    explicit ArenaAllocator(std::size_t capacity)
        : capacity_(capacity), offset_(0) {
        // Reservamos la memoria continua al inicio
        buffer_ = static_cast<std::byte*>(std::malloc(capacity_));
        if (!buffer_) {
            throw std::bad_alloc();
        }
    }

    ~ArenaAllocator() {
        if (buffer_) {
            std::free(buffer_);
            buffer_ = nullptr;
        }
    }

    // Prohibir copia para evitar problemas de doble liberación de memoria
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    // Asignación de memoria cruda alineada
    void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
        std::uintptr_t current_addr = reinterpret_cast<std::uintptr_t>(buffer_ + offset_);
        std::uintptr_t aligned_addr = align_forward(current_addr, alignment);
        
        // Cuántos bytes tuvimos que saltar por alineación
        std::size_t padding = aligned_addr - current_addr;

        if (offset_ + padding + size > capacity_) {
            // Se agotó el espacio en el Arena
            return nullptr;
        }

        offset_ += padding + size;
        return reinterpret_cast<void*>(aligned_addr);
    }

    // Método template: emula "new" construyendo el objeto directamente en el bloque
    template <typename T, typename... Args>
    T* create(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        if (!mem) {
            return nullptr;
        }
        // Placement new: ejecuta el constructor en la dirección "mem" sin pedir memoria al SO
        return new (mem) T(std::forward<Args>(args)...);
    }

    // Resetear en O(1)
    void reset() noexcept {
        offset_ = 0;
    }

    [[nodiscard]] std::size_t get_used_bytes() const noexcept {
        return offset_;
    }

    [[nodiscard]] std::size_t get_capacity() const noexcept {
        return capacity_;
    }
};
