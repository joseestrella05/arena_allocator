#include <iostream>
#include <chrono>
#include <iomanip>
#include <random>
#include <cmath>
#include "Engine.hpp"
#include "Portfolio.hpp"

class RiskManagedVWAPStrategy : public IStrategy {
private:
    Portfolio& portfolio_;
    
    // Parámetros de ejecución
    double entry_price_{0.0};
    double last_price_{0.0};
    
    // Estadísticas incrementales en línea (Algoritmo de Welford para media y varianza)
    uint64_t count_{0};
    double mean_{0.0};
    double m2_{0.0}; // Suma de diferencias al cuadrado respecto a la media

    // Umbrales de riesgo
    static constexpr double STOP_LOSS_PCT = 0.010;   // 1.0% pérdida máxima tolerada
    static constexpr double TAKE_PROFIT_PCT = 0.015; // 1.5% toma de ganancias
    static constexpr double Z_SCORE_BUY = -2.0;      // 2 sigmas por debajo

public:
    explicit RiskManagedVWAPStrategy(Portfolio& p) : portfolio_(p) {}

    void on_tick(const Tick& tick) override {
        last_price_ = tick.price;
        count_++;

        // Actualización de Welford en O(1)
        double delta = tick.price - mean_;
        mean_ += delta / count_;
        double delta2 = tick.price - mean_;
        m2_ += delta * delta2;

        // Necesitamos al menos 100 observaciones para que la desviación estándar tenga sentido
        if (count_ < 100) return;

        double variance = m2_ / (count_ - 1);
        double std_dev = std::sqrt(variance);
        if (std_dev < 1e-6) return;

        double z_score = (tick.price - mean_) / std_dev;

        // 1. Control de posición abierta (Stop-Loss y Take-Profit prioritarios)
        if (portfolio_.get_position() > 0) {
            double pnl_pct = (tick.price - entry_price_) / entry_price_;

            if (pnl_pct <= -STOP_LOSS_PCT) {
                // Ejecución de Stop-Loss para cortar pérdida
                portfolio_.sell(tick.price, 100);
                entry_price_ = 0.0;
            } else if (pnl_pct >= TAKE_PROFIT_PCT || z_score >= 2.0) {
                // Ejecución de Take-Profit o reversión estadística completada
                portfolio_.sell(tick.price, 100);
                entry_price_ = 0.0;
            }
        } 
        // 2. Búsqueda de nueva entrada solo si no tenemos posición abierta
        else if (portfolio_.get_position() == 0) {
            if (z_score <= Z_SCORE_BUY) {
                if (portfolio_.buy(tick.price, 100)) {
                    entry_price_ = tick.price;
                }
            }
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
    RiskManagedVWAPStrategy strategy(portfolio);

    engine.register_strategy(&strategy);

    std::cout << "Ejecutando simulación con Control de Riesgo (Stop-Loss/Take-Profit/Z-Score)..." << std::endl;

    std::mt19937_64 rng(42); // Misma semilla para comparar exactamente contra el test anterior
    std::normal_distribution<double> normal_dist(0.0, 1.0);

    double price = 100.00;
    constexpr double dt = 1.0 / (252.0 * 6.5 * 3600.0);
    constexpr double drift = 0.05;
    constexpr double volatility = 0.20;

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < BATCH_SIZE; ++i) {
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

    portfolio.print_summary(strategy.get_last_price());

    engine.reset_memory();
    return 0;
}
