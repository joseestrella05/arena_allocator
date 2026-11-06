#include <iostream>
#include <chrono>
#include <iomanip>
#include <random>
#include <cmath>
#include "Engine.hpp"
#include "Portfolio.hpp"

class LiveVWAPStrategy : public IStrategy {
private:
    Portfolio& portfolio_;
    double cumulative_dollar_volume_{0.0};
    uint64_t cumulative_volume_{0};
    double last_price_{0.0};

public:
    explicit LiveVWAPStrategy(Portfolio& p) : portfolio_(p) {}

    void on_tick(const Tick& tick) override {
        last_price_ = tick.price;
        cumulative_dollar_volume_ += tick.price * tick.volume;
        cumulative_volume_ += tick.volume;

        double current_vwap = cumulative_dollar_volume_ / cumulative_volume_;
        double deviation = (tick.price - current_vwap) / current_vwap;

        // Reglas de entrada y salida con umbrales ajustados
        if (deviation < -0.002 && portfolio_.get_position() == 0) {
            portfolio_.buy(tick.price, 100);
        } else if (deviation > 0.002 && portfolio_.get_position() > 0) {
            portfolio_.sell(tick.price, 100);
        }
    }

    [[nodiscard]] double get_last_price() const noexcept {
        return last_price_;
    }
};

int main() {
    constexpr size_t BATCH_SIZE = 5'000'000;
    constexpr size_t ARENA_SIZE = 180 * 1024 * 1024; 

    Portfolio portfolio(100'000.0);
    BacktestEngine engine(ARENA_SIZE);
    LiveVWAPStrategy strategy(portfolio);

    engine.register_strategy(&strategy);

    std::cout << "Generando y procesando " << BATCH_SIZE 
              << " ticks con Movimiento Browniano Geométrico (GBM)..." << std::endl;

    // Generador pseudoaleatorio de alta velocidad (Mersenne Twister)
    std::mt19937_64 rng(42); // Semilla fija para reproducibilidad
    std::normal_distribution<double> normal_dist(0.0, 1.0);

    double price = 100.00;
    constexpr double dt = 1.0 / (252.0 * 6.5 * 3600.0); // Paso temporal (segundos de mercado)
    constexpr double drift = 0.05;                      // 5% anual esperado
    constexpr double volatility = 0.20;                 // 20% volatilidad anualizada

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < BATCH_SIZE; ++i) {
        // Fórmula GBM: dS = S * (mu*dt + sigma*sqrt(dt)*Z)
        double z = normal_dist(rng);
        double delta_price = price * (drift * dt + volatility * std::sqrt(dt) * z);
        price += delta_price;

        uint32_t vol = 50 + (i % 100);

        engine.process_tick(
            1700000000000000ULL + i * 1000,
            price,
            vol,
            EventType::TRADE,
            "SPY"
        );
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;

    std::cout << "\nTiempo de simulación: " << elapsed.count() << " ms" << std::endl;
    std::cout << "Throughput         : " 
              << std::fixed << std::setprecision(2)
              << (BATCH_SIZE / (elapsed.count() / 1000.0)) / 1'000'000.0 
              << " M ticks/seg" << std::endl;

    // Reporte cuantitativo con Sharpe y Max Drawdown
    portfolio.print_summary(strategy.get_last_price());

    engine.reset_memory();
    return 0;
}
