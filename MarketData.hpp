#pragma once

#include <cstdint>
#include <string_view>
#include <iostream>

// Tipo de evento que llega desde el mercado
enum class EventType : uint8_t {
    TRADE = 1,
    QUOTE = 2
};

// Estructura optimizada a 32 bytes (múltiplo de 8 para alineación ARM64)
struct alignas(8) Tick {
    uint64_t timestamp_ns; // Tiempo en nanosegundos (Unix epoch)
    double price;          // Precio de la transacción o cotización
    uint32_t volume;       // Cantidad negociada
    EventType type;        // Trade o Quote
    char symbol[7];        // Ticker (ej: "AAPL\0\0\0")

    Tick(uint64_t ts, double p, uint32_t v, EventType t, std::string_view sym)
        : timestamp_ns(ts), price(p), volume(v), type(t) {
        // Copiar el símbolo con terminación nula segura
        size_t len = std::min(sym.size(), sizeof(symbol) - 1);
        for (size_t i = 0; i < len; ++i) {
            symbol[i] = sym[i];
        }
        symbol[len] = '\0';
    }
};
