#pragma once

#include "Arena.hpp"
#include "MarketData.hpp"
#include <vector>

// Interfaz para cualquier estrategia cuantitativa
class IStrategy {
public:
    virtual ~IStrategy() = default;
    virtual void on_tick(const Tick& tick) = 0;
};

// Motor de ejecución basado en eventos
class BacktestEngine {
private:
    ArenaAllocator arena_;
    IStrategy* strategy_{nullptr};
    uint64_t processed_ticks_{0};

public:
    explicit BacktestEngine(size_t arena_capacity)
        : arena_(arena_capacity) {}

    void register_strategy(IStrategy* strategy) {
        strategy_ = strategy;
    }

    // Procesa un evento individual
    void process_tick(uint64_t ts, double price, uint32_t vol, EventType type, std::string_view sym) {
        // Asignamos el tick directamente en nuestra memoria ultra rápida
        Tick* tick = arena_.create<Tick>(ts, price, vol, type, sym);
        if (!tick) {
            std::cerr << "Arena agotada. Es necesario hacer reset().\n";
            return;
        }

        // Enviamos el tick a la estrategia para calcular señales
        if (strategy_) {
            strategy_->on_tick(*tick);
        }

        processed_ticks_++;
    }

    // Limpieza al final de una sesión de mercado o ráfaga de datos
    void reset_memory() noexcept {
        arena_.reset();
    }

    [[nodiscard]] uint64_t get_processed_count() const noexcept {
        return processed_ticks_;
    }

    [[nodiscard]] size_t get_memory_usage() const noexcept {
        return arena_.get_used_bytes();
    }
};
