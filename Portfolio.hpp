#pragma once

#include <cstdint>
#include <iostream>
#include <iomanip>

class Portfolio {
private:
    double initial_cash_{100'000.0}; // Capital inicial: $100,000 USD
    double cash_{100'000.0};
    int64_t current_position_{0};    // Cantidad de acciones en posesión
    double total_commissions_{0.0};
    uint64_t total_trades_{0};

    // Comisión típica de broker institucional/API: $0.005 por acción
    static constexpr double COMMISSION_PER_SHARE = 0.005;

public:
    explicit Portfolio(double start_cash = 100'000.0) 
        : initial_cash_(start_cash), cash_(start_cash) {}

    // Simula una compra (LONG)
    bool buy(double price, uint32_t quantity) {
        double cost = price * quantity;
        double comm = quantity * COMMISSION_PER_SHARE;

        if (cash_ < (cost + comm)) {
            return false; // Fondos insuficientes
        }

        cash_ -= (cost + comm);
        current_position_ += quantity;
        total_commissions_ += comm;
        total_trades_++;
        return true;
    }

    // Simula una venta para cerrar posición
    bool sell(double price, uint32_t quantity) {
        if (current_position_ < quantity) {
            return false; // No hay suficientes acciones para vender
        }

        double proceeds = price * quantity;
        double comm = quantity * COMMISSION_PER_SHARE;

        cash_ += (proceeds - comm);
        current_position_ -= quantity;
        total_commissions_ += comm;
        total_trades_++;
        return true;
    }

    // Valor liquidativo de la cuenta (Cash + valor actual de las acciones al precio de mercado)
    [[nodiscard]] double get_total_equity(double current_price) const noexcept {
        return cash_ + (current_position_ * current_price);
    }

    void print_summary(double final_price) const {
        double final_equity = get_total_equity(final_price);
        double pnl = final_equity - initial_cash_;
        double return_pct = (pnl / initial_cash_) * 100.0;

        std::cout << "\n--- REPORTE FINANCIERO DEL PORTAFOLIO ---" << std::endl;
        std::cout << "Capital Inicial       : $" << std::fixed << std::setprecision(2) << initial_cash_ << std::endl;
        std::cout << "Capital Final (Equity): $" << final_equity << std::endl;
        std::cout << "P&L Neto (Ganancia)   : $" << pnl << " (" << return_pct << "%)" << std::endl;
        std::cout << "Operaciones ejecutadas: " << total_trades_ << std::endl;
        std::cout << "Comisiones pagadas    : $" << total_commissions_ << std::endl;
        std::cout << "Acciones remanentes   : " << current_position_ << std::endl;
    }

    [[nodiscard]] int64_t get_position() const noexcept { return current_position_; }
};
