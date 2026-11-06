#pragma once

#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <numeric>

class Portfolio {
private:
    double initial_cash_{100'000.0};
    double cash_{100'000.0};
    int64_t current_position_{0};
    double total_commissions_{0.0};
    uint64_t total_trades_{0};

    // Tracking de riesgo y volatilidad
    double peak_equity_{100'000.0};
    double max_drawdown_{0.0}; // En porcentaje
    std::vector<double> returns_history_;
    double last_recorded_equity_{100'000.0};

    static constexpr double COMMISSION_PER_SHARE = 0.005;

public:
    explicit Portfolio(double start_cash = 100'000.0) 
        : initial_cash_(start_cash), 
          cash_(start_cash), 
          peak_equity_(start_cash),
          last_recorded_equity_(start_cash) {
        returns_history_.reserve(100'000);
    }

    bool buy(double price, uint32_t quantity) {
        double cost = price * quantity;
        double comm = quantity * COMMISSION_PER_SHARE;

        if (cash_ < (cost + comm)) {
            return false;
        }

        cash_ -= (cost + comm);
        current_position_ += quantity;
        total_commissions_ += comm;
        total_trades_++;
        update_risk_metrics(price);
        return true;
    }

    bool sell(double price, uint32_t quantity) {
        if (current_position_ < quantity) {
            return false;
        }

        double proceeds = price * quantity;
        double comm = quantity * COMMISSION_PER_SHARE;

        cash_ += (proceeds - comm);
        current_position_ -= quantity;
        total_commissions_ += comm;
        total_trades_++;
        update_risk_metrics(price);
        return true;
    }

    void update_risk_metrics(double current_price) {
        double current_equity = get_total_equity(current_price);

        // Registro de retorno logarítmico/porcentual de la operación
        if (last_recorded_equity_ > 0.0) {
            double ret = (current_equity - last_recorded_equity_) / last_recorded_equity_;
            returns_history_.push_back(ret);
        }
        last_recorded_equity_ = current_equity;

        // Cálculo de Drawdown continuo
        if (current_equity > peak_equity_) {
            peak_equity_ = current_equity;
        } else {
            double drawdown = (peak_equity_ - current_equity) / peak_equity_;
            if (drawdown > max_drawdown_) {
                max_drawdown_ = drawdown;
            }
        }
    }

    [[nodiscard]] double get_total_equity(double current_price) const noexcept {
        return cash_ + (current_position_ * current_price);
    }

    [[nodiscard]] double calculate_sharpe_ratio() const {
        if (returns_history_.size() < 2) return 0.0;

        // Media de los retornos
        double sum = std::accumulate(returns_history_.begin(), returns_history_.end(), 0.0);
        double mean = sum / returns_history_.size();

        // Desviación estándar
        double variance = 0.0;
        for (double r : returns_history_) {
            variance += (r - mean) * (r - mean);
        }
        variance /= (returns_history_.size() - 1);
        double std_dev = std::sqrt(variance);

        if (std_dev == 0.0) return 0.0;

        // Factor de anualización estimado para datos intradía de alta frecuencia
        // Suponiendo ~252 días de bolsa al año
        double annualization_factor = std::sqrt(252.0 * 390.0); // 390 minutos por sesión
        return (mean / std_dev) * annualization_factor;
    }

    void print_summary(double final_price) {
        update_risk_metrics(final_price);
        double final_equity = get_total_equity(final_price);
        double pnl = final_equity - initial_cash_;
        double return_pct = (pnl / initial_cash_) * 100.0;
        double sharpe = calculate_sharpe_ratio();

        std::cout << "\n==========================================" << std::endl;
        std::cout << "        REPORTE DE RENDIMIENTO Y RIESGO   " << std::endl;
        std::cout << "==========================================" << std::endl;
        std::cout << "Capital Inicial        : $" << std::fixed << std::setprecision(2) << initial_cash_ << std::endl;
        std::cout << "Capital Final (Equity) : $" << final_equity << std::endl;
        std::cout << "P&L Neto (Ganancia)    : $" << pnl << " (" << return_pct << "%)" << std::endl;
        std::cout << "Operaciones ejecutadas : " << total_trades_ << std::endl;
        std::cout << "Comisiones pagadas     : $" << total_commissions_ << std::endl;
        std::cout << "Acciones remanentes    : " << current_position_ << std::endl;
        std::cout << "------------------------------------------" << std::endl;
        std::cout << "Max Drawdown (Caída)   : " << (max_drawdown_ * 100.0) << "%" << std::endl;
        std::cout << "Sharpe Ratio (Anual)   : " << std::setprecision(3) << sharpe << std::endl;
        std::cout << "==========================================" << std::endl;
    }

    [[nodiscard]] int64_t get_position() const noexcept { return current_position_; }
};
