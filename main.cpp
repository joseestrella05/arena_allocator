#include <iostream>
#include <chrono>
#include <iomanip>
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

        // Lógica de Trading Cuantitativo:
        // Compra si está infravalorado respecto a la media de volumen
        if (deviation < -0.003 && portfolio_.get_position() == 0) {
            portfolio_.buy(tick.price, 100);
        }
        // Vende para cerrar beneficio cuando vuelve por encima de la media
        else if (deviation > 0.003 && portfolio_.get_position() > 0) {
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

    Portfolio portfolio(100'000.0); // Iniciamos con $100,000
    BacktestEngine engine(ARENA_SIZE);
    LiveVWAPStrategy strategy(portfolio);

    engine.register_strategy(&strategy);

    std::cout << "Ejecutando simulación de trading cuantitativo sobre " 
              << BATCH_SIZE << " ticks..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    // Simulación de serie temporal con caminata pseudoaleatoria
    double price = 150.00;
    for (size_t i = 0; i < BATCH_SIZE; ++i) {
        // Fluctuación senoidal + tendencia para emular volatilidad intradía
        double noise = std::sin(static_cast<double>(i) * 0.001) * 0.50;
        double current_price = price + noise;
        uint32_t vol = 50 + (i % 150);

        engine.process_tick(
            1700000000000000ULL + i * 1000,
            current_price,
            vol,
            EventType::TRADE,
            "NVDA"
        );
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;

    std::cout << "\nTiempo total de ejecución: " << elapsed.count() << " ms" << std::endl;
    std::cout << "Throughput: " 
              << std::fixed << std::setprecision(2)
              << (BATCH_SIZE / (elapsed.count() / 1000.0)) / 1'000'000.0 
              << " Millones de ticks/segundo" << std::endl;

    // Reporte contable
    portfolio.print_summary(strategy.get_last_price());

    engine.reset_memory();
    return 0;
}
