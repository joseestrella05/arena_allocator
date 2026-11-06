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
    double current_position_{0.0}; // Cantidad de activo (ej. BTC)
    double total_commissions_{0.0};
    double total_slippage_loss_{0.0};
    uint64_t total_trades_{0};

    // Parámetros de mercado real:
    // Comisión Taker: 0.04% (0.0004) estándar para cuentas institucionales/VIP
    static constexpr double TAKER_FEE_PCT = 0.0004; 
    // Slippage medio por cruzar el spread en rupturas de volatilidad: 0.02% (2 bps)
    static constexpr double SLIPPAGE_PCT = 0.0002;

    double peak_equity_{100'000.0};
    double max_drawdown_{0.0};
    std::vector<double> returns_history_;
    double last_recorded_equity_{100'000.0};

public:
    explicit Portfolio(double start_cash = 100'000.0) 
        : initial_cash_(start_cash), 
          cash_(start_cash), 
          peak_equity_(start_cash),
          last_recorded_equity_(start_cash) {
        returns_history_.reserve(100'000);
    }

    // Compra a mercado: paga precio + slippage (Ask) + comisión porcentual
    bool buy(double execution_price, double quantity) {
        double real_buy_price = execution_price * (1.0 + SLIPPAGE_PCT);
        double notional = real_buy_price * quantity;
        double commission = notional * TAKER_FEE_PCT;
        double slippage_cost = (real_buy_price - execution_price) * quantity;

        if (cash_ < (notional + commission)) {
            return false;
        }

        cash_ -= (notional + commission);
        current_position_ += quantity;
        total_commissions_ += commission;
        total_slippage_loss_ += slippage_cost;
        total_trades_++;

        update_risk_metrics(execution_price);
        return true;
    }

    // Venta a mercado: recibe precio - slippage (Bid) - comisión porcentual
    bool sell(double execution_price, double quantity) {
        if (current_position_ < quantity) {
            return false;
        }

        double real_sell_price = execution_price * (1.0 - SLIPPAGE_PCT);
        double notional = real_sell_price * quantity;
        double commission = notional * TAKER_FEE_PCT;
        double slippage_cost = (execution_price - real_sell_price) * quantity;

        cash_ += (notional - commission);
        current_position_ -= quantity;
        total_commissions_ += commission;
        total_slippage_loss_ += slippage_cost;
        total_trades_++;

        update_risk_metrics(execution_price);
        return true;
    }

    void update_risk_metrics(double current_price) {
        double current_equity = get_total_equity(current_price);

        if (last_recorded_equity_ > 0.0) {
            double ret = (current_equity - last_recorded_equity_) / last_recorded_equity_;
            returns_history_.push_back(ret);
        }
        last_recorded_equity_ = current_equity;

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

        double sum = std::accumulate(returns_history_.begin(), returns_history_.end(), 0.0);
        double mean = sum / returns_history_.size();

        double variance = 0.0;
        for (double r : returns_history_) {
            variance += (r - mean) * (r - mean);
        }
        variance /= (returns_history_.size() - 1);
        double std_dev = std::sqrt(variance);

        if (std_dev < 1e-8) return 0.0;

        // Anualización para intradía: sqrt(365 * 24 * 60) en crypto
        double annualization_factor = std::sqrt(365.0 * 24.0 * 60.0);
        return (mean / std_dev) * annualization_factor;
    }

    void print_summary(double final_price) {
        update_risk_metrics(final_price);
        double final_equity = get_total_equity(final_price);
        double pnl = final_equity - initial_cash_;
        double return_pct = (pnl / initial_cash_) * 100.0;
        double sharpe = calculate_sharpe_ratio();

        std::cout << "\n==========================================" << std::endl;
        std::cout << "  REPORTE CON FRICCIÓN Y SLIPPAGE REAL   " << std::endl;
        std::cout << "==========================================" << std::endl;
        std::cout << "Capital Inicial        : $" << std::fixed << std::setprecision(2) << initial_cash_ << std::endl;
        std::cout << "Capital Final (Equity) : $" << final_equity << std::endl;
        std::cout << "P&L Neto (Ganancia)    : $" << pnl << " (" << return_pct << "%)" << std::endl;
        std::cout << "Operaciones ejecutadas : " << total_trades_ << std::endl;
        std::cout << "Comisiones pagadas     : $" << total_commissions_ << std::endl;
        std::cout << "Pérdida por Slippage   : $" << total_slippage_loss_ << std::endl;
        std::cout << "Costo total de fricción: $" << (total_commissions_ + total_slippage_loss_) << std::endl;
        std::cout << "Posición remanente     : " << current_position_ << " BTC" << std::endl;
        std::cout << "------------------------------------------" << std::endl;
        std::cout << "Max Drawdown (Caída)   : " << (max_drawdown_ * 100.0) << "%" << std::endl;
        std::cout << "Sharpe Ratio (Anual)   : " << std::setprecision(3) << sharpe << std::endl;
        std::cout << "==========================================" << std::endl;
    }

    [[nodiscard]] double get_position() const noexcept { return current_position_; }
};
