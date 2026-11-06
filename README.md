# High-Throughput Zero-Copy Quantitative Backtesting Engine

Un motor de backtesting de alto rendimiento y ultra-baja latencia en **C++20** optimizado para arquitecturas **Apple Silicon (ARM64)**. Diseñado desde primeros principios para eliminar la sobrecarga del asignador del sistema operativo (*heap*) y cuellos de botella de E/S mediante memoria virtual mapeada (`mmap`), procesamiento *zero-copy* y asignación lineal en bloques contiguos.

El motor fue validado y sometido a pruebas de estrés contra más de **1.11 millones de transacciones de mercado reales** de Bitcoin (`BTC/USDT`) provenientes de Binance, modelando microestructura financiera estricta (comisiones *taker* porcentuales y penalización por *slippage*).

---

## Métricas de Rendimiento (Hardware Benchmarks)

Pruebas ejecutadas en **Apple Silicon (M-Series ARM64)** con compilador Clang (`-std=c++20 -O3`):

| Componente / Prueba | Métrica | Rendimiento Obtenido |
| :--- | :--- | :--- |
| **Arena Allocator vs Heap** | 1,000,000 estructuras `Order` | **4.67 ms** (vs 13.98 ms `malloc` / 3x más rápido) |
| **Throughput de Backtest (mmap)** | 1,114,623 trades reales | **7.04 Millones de ticks/segundo** |
| **Tiempo Total de Ejecución** | 1.11M ticks + contabilidad de riesgo | **158.37 ms** |
| **Latencia de Asignación Arena** | Inserción + alineación de hardware | **$\mathcal{O}(1)$ determinista** |
| **Liberación de Memoria Global** | Reinicio de puntero de arena | **$\mathcal{O}(1)$ instantáneo** |

---

## Arquitectura de Sistemas de Bajo Nivel
