#include <iostream>
#include <chrono>
#include <iomanip>
#include <deque>
#include <cmath>
#include <algorithm>
#include "Engine.hpp"
#include "Portfolio.hpp"
#include "FastFileReader.hpp"

inline uint64_t parse_uint64(const char*& ptr) {
    uint64_t val = 0;
    while (*ptr >= '0' && *ptr <= '9') {
        val = val * 10 + (*ptr - '0');
        ptr++;
    }
    return val;
}

inline double parse_double(const char*& ptr) {
    double int_part = 0.0;
    while (*ptr >= '0' && *ptr <= '9') {
        int_part = int_part * 10.0 + (*ptr - '0');
        ptr++;
    }
    if (*ptr == '.') {
        ptr++;
        double factor = 0.1;
        while (*ptr >= '0' && *ptr <= '9') {
            int_part += (*ptr - '0') * factor;
            factor *= 0.1;
            ptr++;
        }
    }
    return int_part;
}

inline void skip_column(const char*& ptr, const char* end) {
    while (ptr < end && *ptr != ',' && *ptr != '\n') ptr++;
    if (ptr < end && *ptr == ',') ptr++;
}

class MomentumBreakoutStrategy : public IStrategy {
private:
    Portfolio& portfolio_;
    double entry_price_{0.0};
    double highest_price_since_entry_{0.0};
    double last_price_{0.0};

    static constexpr double POSITION_SIZE_BTC = 0.5; // 0.5 BTC por operación
    static constexpr size_t WINDOW_SIZE = 600;
    std::deque<double> price_window_;
    std::deque<uint32_t> volume_window_;
    uint64_t volume_sum_{0};

    static constexpr double INITIAL_STOP_PCT = 0.005;  // 0.5% stop inicial
    static constexpr double TRAILING_STOP_PCT = 0.004; // 0.4% trailing stop
    
    uint64_t ticks_since_exit_{1000};
    static constexpr uint64_t COOLDOWN_TICKS = 400;

public:
    explicit MomentumBreakoutStrategy(Portfolio& p) : portfolio_(p) {}

    void on_tick(const Tick& tick) override {
        last_price_ = tick.price;
        ticks_since_exit_++;

        price_window_.push_back(tick.price);
        volume_window_.push_back(tick.volume);
        volume_sum_ += tick.volume;

        if (price_window_.size() > WINDOW_SIZE) {
            price_window_.pop_front();
            volume_sum_ -= volume_window_.front();
            volume_window_.pop_front();
        }

        if (price_window_.size() < WINDOW_SIZE) return;

        // 1. Gestión de Posición Abierta con Trailing Stop
        if (portfolio_.get_position() > 0.0) {
            if (tick.price > highest_price_since_entry_) {
                highest_price_since_entry_ = tick.price;
            }

            double trailing_stop_price = highest_price_since_entry_ * (1.0 - TRAILING_STOP_PCT);
            double hard_stop_price = entry_price_ * (1.0 - INITIAL_STOP_PCT);
            double exit_trigger = std::max(trailing_stop_price, hard_stop_price);

            if (tick.price <= exit_trigger) {
                portfolio_.sell(tick.price, POSITION_SIZE_BTC);
                entry_price_ = 0.0;
                highest_price_since_entry_ = 0.0;
                ticks_since_exit_ = 0;
            }
        } 
        // 2. Búsqueda de Ruptura con Flujo de Volumen
        else if (portfolio_.get_position() == 0.0 && ticks_since_exit_ >= COOLDOWN_TICKS) {
            double prev_high = 0.0;
            for (size_t i = 0; i < price_window_.size() - 1; ++i) {
                if (price_window_[i] > prev_high) {
                    prev_high = price_window_[i];
                }
            }

            double avg_volume = static_cast<double>(volume_sum_) / WINDOW_SIZE;

            // Filtro estricto: Ruptura del máximo + volumen institucional (1.3x media)
            if (tick.price > prev_high && tick.volume > (avg_volume * 1.3)) {
                if (portfolio_.buy(tick.price, POSITION_SIZE_BTC)) {
                    entry_price_ = tick.price;
                    highest_price_since_entry_ = tick.price;
                }
            }
        }
    }

    [[nodiscard]] double get_last_price() const noexcept { return last_price_; }
};

int main() {
    constexpr size_t ARENA_SIZE = 128 * 1024 * 1024;

    const std::string datafile = "BTCUSDT-trades-2024-01-01.csv";
    std::cout << "Mapeando '" << datafile << "' a memoria con mmap..." << std::endl;
    FastFileReader file_reader(datafile);

    Portfolio portfolio(100'000.0);
    BacktestEngine engine(ARENA_SIZE);
    MomentumBreakoutStrategy strategy(portfolio);
    engine.register_strategy(&strategy);

    const char* curr = file_reader.data();
    const char* end = curr + file_reader.size();

    std::cout << "Evaluando Momentum con comisiones Taker (0.04%) y Slippage (0.02%)...\n";
    auto start = std::chrono::high_resolution_clock::now();

    size_t lines_read = 0;
    while (curr < end) {
        skip_column(curr, end); // trade_id

        double price = parse_double(curr);
        if (curr < end && *curr == ',') curr++;

        double qty = parse_double(curr);
        if (curr < end && *curr == ',') curr++;

        skip_column(curr, end); // quote_qty

        uint64_t ts_ms = parse_uint64(curr);
        uint64_t ts_ns = ts_ms * 1'000'000ULL;

        while (curr < end && *curr != '\n') curr++;
        if (curr < end) curr++;

        engine.process_tick(ts_ns, price, static_cast<uint32_t>(qty * 100), EventType::TRADE, "BTC");
        lines_read++;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - start;

    std::cout << "\nTiempo total de ejecución: " << elapsed.count() << " ms" << std::endl;
    std::cout << "Trades reales procesados : " << lines_read << std::endl;
    std::cout << "Throughput               : "
              << std::fixed << std::setprecision(2)
              << (lines_read / (elapsed.count() / 1000.0)) / 1'000'000.0
              << " M ticks/seg" << std::endl;

    portfolio.print_summary(strategy.get_last_price());
    engine.reset_memory();

    return 0;
}
