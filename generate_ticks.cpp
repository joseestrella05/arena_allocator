#include <iostream>
#include <fstream>
#include <random>
#include <cmath>
#include <vector>
#include <iomanip>

int main() {
    constexpr size_t NUM_TICKS = 2'000'000;
    const std::string filename = "ticks.csv";

    std::cout << "Generando " << NUM_TICKS << " registros en " << filename << " usando C++..." << std::endl;

    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error al abrir el archivo para escritura.\n";
        return 1;
    }

    file << "timestamp,symbol,price,volume,type\n";

    // Proceso Ornstein-Uhlenbeck (Reversión a la media)
    constexpr double theta = 0.15;
    constexpr double mu = 150.0;
    constexpr double sigma = 0.5;
    constexpr double dt = 0.01;
    const double sqrt_dt = std::sqrt(dt);

    std::mt19937_64 rng(1337);
    std::normal_distribution<double> norm_dist(0.0, 1.0);
    std::uniform_int_distribution<uint32_t> vol_dist(10, 500);

    double price = 150.0;
    uint64_t base_ts = 1700000000000000000ULL;

    for (size_t i = 0; i < NUM_TICKS; ++i) {
        price += theta * (mu - price) * dt + sigma * sqrt_dt * norm_dist(rng);
        uint32_t volume = vol_dist(rng);
        uint64_t ts = base_ts + i * 1000;

        file << ts << ",SPY," 
             << std::fixed << std::setprecision(2) << price << "," 
             << volume << ",TRADE\n";
    }

    file.close();
    std::cout << "Archivo " << filename << " generado exitosamente." << std::endl;
    return 0;
}
