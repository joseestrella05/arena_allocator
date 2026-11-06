#include <iostream>
#include <chrono>
#include <iomanip>
#include <charconv>
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

class FastTickStrategy : public IStrategy {
private:
    Portfolio& portfolio_;
    double entry_price_{0.0};
    double last_price_{0.0};

    uint64_t count_{0};
    double mean_{0.0};
    double m2_{0.0};

    static constexpr double STOP_LOSS_PCT = 0.008;   // 0.8% stop
    static constexpr double TAKE_PROFIT_PCT = 0.012; // 1.2% take-profit

public:
    explicit FastTickStrategy(Portfolio& p) : portfolio_(p) {}

    void on_tick(const Tick& tick) override {
        last_price_ = tick.price;
        count_++;

        double delta = tick.price - mean_;
        mean_ += delta / count_;
        double delta2 = tick.price - mean_;
        m2_ += delta * delta2;

        if (count_ < 200) return;

        double variance = m2_ / (count_ - 1);
        double std_dev = std::sqrt(variance);
        if (std_dev < 1e-6) return;

        double z_score = (tick.price - mean_) / std_dev;

        if (portfolio_.get_position() > 0) {
            double pnl_pct = (tick.price - entry_price_) / entry_price_;
            if (pnl_pct <= -STOP_LOSS_PCT || pnl_pct >= TAKE_PROFIT_PCT || z_score >= 1.8) {
                portfolio_.sell(tick.price, 100);
                entry_price_ = 0.0;
            }
        } else if (portfolio_.get_position() == 0) {
            if (z_score <= -1.8) {
                if (portfolio_.buy(tick.price, 100)) {
                    entry_price_ = tick.price;
                }
            }
        }
    }

    [[nodiscard]] double get_last_price() const noexcept { return last_price_; }
};

int main() {
    constexpr size_t ARENA_SIZE = 128 * 1024 * 1024; // 128 MB

    std::cout << "Mapeando 'ticks.csv' a memoria con mmap..." << std::endl;
    FastFileReader file_reader("ticks.csv");

    Portfolio portfolio(100'000.0);
    BacktestEngine engine(ARENA_SIZE);
    FastTickStrategy strategy(portfolio);
    engine.register_strategy(&strategy);

    const char* curr = file_reader.data();
    const char* end = curr + file_reader.size();

    // Saltar cabecera
    while (curr < end && *curr != '\n') curr++;
    if (curr < end) curr++;

    std::cout << "Iniciando procesamiento zero-copy desde memoria mapeada...\n";
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

    std::cout << "\nTiempo total (mmap + parse + backtest): " << elapsed.count() << " ms" << std::endl;
    std::cout << "Lineas leidas y procesadas           : " << lines_read << std::endl;
    std::cout << "Throughput global                    : "
              << std::fixed << std::setprecision(2)
              << (lines_read / (elapsed.count() / 1000.0)) / 1'000'000.0
              << " M ticks/seg" << std::endl;

    portfolio.print_summary(strategy.get_last_price());
    engine.reset_memory();

    return 0;
}
