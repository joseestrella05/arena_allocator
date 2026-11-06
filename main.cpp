#include <iostream>
#include <chrono>
#include <iomanip>
#include <deque>
#include <cmath>
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

class RollingZScoreStrategy : public IStrategy {
private:
    Portfolio& portfolio_;
    double entry_price_{0.0};
    double last_price_{0.0};

    static constexpr size_t WINDOW_SIZE = 200;
    std::deque<double> price_window_;
    double rolling_sum_{0.0};

    // Parámetros de control de riesgo para volatilidad de Bitcoin
    static constexpr double STOP_LOSS_PCT = 0.004;   // 0.4% Stop Loss
    static constexpr double TAKE_PROFIT_PCT = 0.006; // 0.6% Take Profit
    static constexpr double Z_ENTRY = 2.2;           // 2.2 desviaciones estándar

public:
    explicit RollingZScoreStrategy(Portfolio& p) : portfolio_(p) {}

    void on_tick(const Tick& tick) override {
        last_price_ = tick.price;

        price_window_.push_back(tick.price);
        rolling_sum_ += tick.price;

        if (price_window_.size() > WINDOW_SIZE) {
            rolling_sum_ -= price_window_.front();
            price_window_.pop_front();
        }

        if (price_window_.size() < WINDOW_SIZE) return;

        double mean = rolling_sum_ / WINDOW_SIZE;

        double var_sum = 0.0;
        for (double p : price_window_) {
            var_sum += (p - mean) * (p - mean);
        }
        double std_dev = std::sqrt(var_sum / WINDOW_SIZE);
        if (std_dev < 1e-4) return;

        double z_score = (tick.price - mean) / std_dev;

        // 1. Gestión de posición abierta
        if (portfolio_.get_position() > 0) {
            double pnl_pct = (tick.price - entry_price_) / entry_price_;

            if (pnl_pct <= -STOP_LOSS_PCT) {
                portfolio_.sell(tick.price, 1);
                entry_price_ = 0.0;
            } else if (pnl_pct >= TAKE_PROFIT_PCT || z_score >= 1.8) {
                portfolio_.sell(tick.price, 1);
                entry_price_ = 0.0;
            }
        } 
        // 2. Entrada por sobreventa estadística
        else if (portfolio_.get_position() == 0) {
            if (z_score <= -Z_ENTRY) {
                if (portfolio_.buy(tick.price, 1)) {
                    entry_price_ = tick.price;
                }
            }
        }
    }

    [[nodiscard]] double get_last_price() const noexcept { return last_price_; }
};

int main() {
    constexpr size_t ARENA_SIZE = 128 * 1024 * 1024; // 128 MB

    const std::string datafile = "BTCUSDT-trades-2024-01-01.csv";
    std::cout << "Mapeando '" << datafile << "' a memoria con mmap..." << std::endl;
    FastFileReader file_reader(datafile);

    Portfolio portfolio(100'000.0);
    BacktestEngine engine(ARENA_SIZE);
    RollingZScoreStrategy strategy(portfolio);
    engine.register_strategy(&strategy);

    const char* curr = file_reader.data();
    const char* end = curr + file_reader.size();

    // El archivo de Binance no tiene cabecera de texto, empieza directamente con datos
    std::cout << "Ejecutando simulación sobre un día completo de mercado real...\n";
    auto start = std::chrono::high_resolution_clock::now();

    size_t lines_read = 0;
    while (curr < end) {
        // Formato: id,price,qty,quote_qty,time,is_buyer_maker,is_best_match
        skip_column(curr, end); // saltar trade_id

        double price = parse_double(curr);
        if (curr < end && *curr == ',') curr++;

        double qty = parse_double(curr);
        if (curr < end && *curr == ',') curr++;

        skip_column(curr, end); // saltar quote_qty

        uint64_t ts_ms = parse_uint64(curr);
        uint64_t ts_ns = ts_ms * 1'000'000ULL;

        // Saltar el resto de columnas hasta el fin de línea
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
