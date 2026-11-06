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

class RollingZScoreStrategy : public IStrategy {
private:
    Portfolio& portfolio_;
    double entry_price_{0.0};
    double last_price_{0.0};

    // Ventana rodante para no sesgar la media con datos viejos
    static constexpr size_t WINDOW_SIZE = 100;
    std::deque<double> price_window_;
    double rolling_sum_{0.0};

    // Parámetros de riesgo para datos reales
    static constexpr double STOP_LOSS_PCT = 0.005;   // 0.5% Stop loss
    static constexpr double TAKE_PROFIT_PCT = 0.008; // 0.8% Take profit
    static constexpr double Z_ENTRY = 2.0;           // 2 desviaciones estándar

public:
    explicit RollingZScoreStrategy(Portfolio& p) : portfolio_(p) {}

    void on_tick(const Tick& tick) override {
        last_price_ = tick.price;

        // Mantenimiento de la ventana rodante en O(1)
        price_window_.push_back(tick.price);
        rolling_sum_ += tick.price;

        if (price_window_.size() > WINDOW_SIZE) {
            rolling_sum_ -= price_window_.front();
            price_window_.pop_front();
        }

        if (price_window_.size() < WINDOW_SIZE) return;

        double mean = rolling_sum_ / WINDOW_SIZE;

        // Varianza sobre la ventana
        double var_sum = 0.0;
        for (double p : price_window_) {
            var_sum += (p - mean) * (p - mean);
        }
        double std_dev = std::sqrt(var_sum / WINDOW_SIZE);
        if (std_dev < 1e-4) return;

        double z_score = (tick.price - mean) / std_dev;

        // 1. Gestión de Posición Abierta
        if (portfolio_.get_position() > 0) {
            double pnl_pct = (tick.price - entry_price_) / entry_price_;

            if (pnl_pct <= -STOP_LOSS_PCT) {
                portfolio_.sell(tick.price, 1); // 1 unidad
                entry_price_ = 0.0;
            } else if (pnl_pct >= TAKE_PROFIT_PCT || z_score >= 1.5) {
                portfolio_.sell(tick.price, 1);
                entry_price_ = 0.0;
            }
        } 
        // 2. Entrada: Comprar cuando hay sobreventa estadística local
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
    constexpr size_t ARENA_SIZE = 64 * 1024 * 1024; // 64 MB suficiente para este lote

    const std::string datafile = "real_ticks.csv";
    std::cout << "Mapeando '" << datafile << "' a memoria con mmap..." << std::endl;
    FastFileReader file_reader(datafile);

    // Ajustamos capital inicial a $100,000 USD
    Portfolio portfolio(100'000.0);
    BacktestEngine engine(ARENA_SIZE);
    RollingZScoreStrategy strategy(portfolio);
    engine.register_strategy(&strategy);

    const char* curr = file_reader.data();
    const char* end = curr + file_reader.size();

    // Saltar cabecera
    while (curr < end && *curr != '\n') curr++;
    if (curr < end) curr++;

    std::cout << "Ejecutando simulación sobre transacciones reales de mercado...\n";
    auto start = std::chrono::high_resolution_clock::now();

    size_t lines_read = 0;
    while (curr < end) {
        uint64_t ts = parse_uint64(curr);
        if (*curr == ',') curr++;

        const char* sym_start = curr;
        while (curr < end && *curr != ',') curr++;
        std::string_view sym(sym_start, curr - sym_start);
        if (*curr == ',') curr++;

        double price = parse_double(curr);
        if (*curr == ',') curr++;

        uint32_t vol = static_cast<uint32_t>(parse_uint64(curr));
        while (curr < end && *curr != '\n') curr++;
        if (curr < end) curr++;

        engine.process_tick(ts, price, vol, EventType::TRADE, sym);
        lines_read++;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - start;

    std::cout << "\nTiempo total de ejecución: " << elapsed.count() << " ms" << std::endl;
    std::cout << "Transacciones reales procesadas: " << lines_read << std::endl;
    std::cout << "Throughput: "
              << std::fixed << std::setprecision(2)
              << (lines_read / (elapsed.count() / 1000.0)) / 1'000'000.0
              << " M ticks/seg" << std::endl;

    portfolio.print_summary(strategy.get_last_price());
    engine.reset_memory();

    return 0;
}
