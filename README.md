# arena-allocator

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![macOS ARM64](https://img.shields.io/badge/macOS-ARM64-lightgrey.svg)
![Clang -O3](https://img.shields.io/badge/Clang--O3-orange.svg)
![License MIT](https://img.shields.io/badge/License-MIT-green.svg)

Motor de backtesting cuantitativo de ultra-baja latencia, diseñado para operar sin sobrecarga del heap en la ruta crítica. Combinamos un asignador lineal (bump allocator) sobre memoria plana preasignada, lectura de datos de mercado por mapeo directo de archivos a memoria virtual (zero-copy) y parsers numéricos escritos desde cero. El resultado es un pipeline determinista que procesa **1,114,623 trades reales de Binance Spot BTC/USDT en 158.37 ms (7.04 M ticks/s)**, incluyendo I/O, parsing, evaluación de estrategia y cálculo de riesgo.

**Entorno**

| Componente | Detalle |
|---|---|
| Estándar | C++20 |
| Compilador | Apple Clang, `-std=c++20 -O3 -Wall -Wextra` |
| Arquitectura objetivo | Apple Silicon ARM64 (localidad de caché L1/L2, alineación de hardware) |

---

## Tabla de contenidos

1. [Benchmarks y latencias](#1-benchmarks-y-latencias)
2. [Flujo de datos](#2-flujo-de-datos)
3. [Ingeniería en detalle](#3-ingeniería-en-detalle)
4. [Reporte de riesgo y backtesting](#4-reporte-de-riesgo-y-backtesting)
5. [Estructura del repositorio](#5-estructura-del-repositorio)
6. [Compilación y ejecución](#6-compilación-y-ejecución)
7. [Licencia](#licencia)

---

## 1. Benchmarks y latencias

| Métrica | Resultado | Referencia / Comparación |
|---|---|---|
| Asignación de 1,000,000 de structs `Order` | **4.67 ms** | `malloc` de libc: 13.98 ms (**~3x más rápido**) |
| Complejidad de asignación | O(1) determinista | Alineación + avance de puntero |
| Reset global de memoria | O(1) | Invalidación instantánea de punteros, sin destructores individuales |
| Dataset procesado | 1,114,623 trades | `BTCUSDT-trades-2024-01-01.csv` |
| Throughput global | **7.04 M ticks/s** | Ruta completa |
| Latencia total de corrida | **158.37 ms** | I/O + parsing + estrategia + riesgo |
| Syscalls de asignación en ruta crítica | 0 | Sin `brk`/`sbrk` |

---

## 2. Flujo de datos

```
 BTCUSDT-trades-2024-01-01.csv  (disco)
            │
            ▼
 ┌──────────────────────────────┐
 │ FastFileReader               │  open → fstat → mmap → madvise
 │ mmap + MADV_WILLNEED |       │  sin read(), sin copias a user-space
 │ MADV_SEQUENTIAL              │
 └──────────────┬───────────────┘
                │  const char* sobre los marcos de página del kernel
                ▼
 ┌──────────────────────────────┐
 │ MarketData                   │  parse_uint64 / parse_double
 │ parsers propios              │  sin std::string, stringstream,
 │                              │  strtod ni regex
 └──────────────┬───────────────┘
                │  Tick
                ▼
 ┌──────────────────────────────┐        ┌──────────────────────────┐
 │ Engine                       │◄──────►│ ArenaAllocator           │
 │ Donchian breakout            │ alloc  │ bump pointer, 128 MB     │
 │ + filtro de volumen 1.3x     │ O(1)   │ reset O(1)               │
 │ + Hard Stop / Trailing Stop  │        └──────────────────────────┘
 └──────────────┬───────────────┘
                │  Order
                ▼
 ┌──────────────────────────────┐
 │ Portfolio                    │  Taker 0.04% + slippage 0.02%
 │ PnL, fricción, Max DD,       │
 │ Sharpe anualizado            │
 └──────────────┬───────────────┘
                ▼
        Reporte en terminal
```

---

## 3. Ingeniería en detalle

### 3.1 Gestión de memoria: `ArenaAllocator`

Implementamos un asignador lineal (bump allocator) sobre un bloque contiguo preasignado (128 MB por defecto). Cada asignación consiste en alinear y avanzar un puntero, sin búsqueda de bloques libres ni metadatos por objeto.

- **Sin syscalls en ruta crítica:** toda la memoria se reserva una sola vez al inicio. Eliminamos por completo las llamadas de asignación (`brk`/`sbrk`) y la fragmentación del heap de libc durante el procesamiento.
- **Alineación estricta de hardware:** cada asignación respeta `alignof(std::max_align_t)`, garantizando accesos alineados en Apple Silicon.
- **Asignación O(1) determinista:** costo constante, en el orden de nanosegundos, sin varianza por estado del heap.
- **Reset global O(1):** `reset()` devuelve el puntero de avance al origen. Todos los punteros previos quedan invalidados al instante, sin ejecutar destructores individuales.
- **Localidad de caché:** los objetos asignados consecutivamente quedan contiguos, favoreciendo el prefetch y la ocupación de líneas de caché L1/L2.

**Resultado empírico:** 1,000,000 de estructuras `Order` asignadas en **4.67 ms**, frente a **13.98 ms** con `malloc` de libc (~3x).

### 3.2 I/O zero-copy: `FastFileReader`

El archivo de trades se mapea directamente al espacio de direcciones virtual del proceso mediante POSIX:

1. `open` abre el descriptor.
2. `fstat` obtiene el tamaño del archivo.
3. `mmap` mapea el archivo en modo lectura.
4. `madvise(MADV_WILLNEED | MADV_SEQUENTIAL)` informa al kernel el patrón de acceso para precargar páginas y minimizar page faults síncronos.
5. `close` libera el descriptor; el mapeo permanece válido.

El motor procesa los bytes directamente desde los marcos de página del kernel mediante punteros `const char*`, sin copias intermedias y sin instanciar `std::string`.

### 3.3 Parsing y estrategia de Momentum

Los tipos primitivos se decodifican con parsers en línea escritos desde cero, `parse_uint64` y `parse_double`. Operan sobre `const char*` y eliminan las asignaciones ocultas de `std::stringstream`, `strtod` y expresiones regulares.

**Estrategia: Momentum Breakout**

| Componente | Parámetro |
|---|---|
| Señal de entrada | Ruptura del canal Donchian |
| Filtro de volumen institucional | Volumen de tick ≥ 1.3x su media móvil |
| Hard Stop inicial | 0.5% |
| Trailing Stop dinámico | 0.4% |

**Modelo de fricción institucional en `Portfolio`**

| Concepto | Valor |
|---|---|
| Comisión Taker | 0.04% (4 bps) |
| Penalización de slippage | 0.02% (2 bps) |

Cada ejecución se penaliza con ambos costos, por lo que el P&L reportado es neto de fricción.

---

## 4. Reporte de riesgo y backtesting

**Dataset:** 1,114,623 transacciones reales consecutivas de Binance Spot BTC/USDT (`BTCUSDT-trades-2024-01-01.csv`).

```text
==========================================
  REPORTE CON FRICCIÓN Y SLIPPAGE REAL
==========================================
Capital Inicial        : $100000.00
Capital Final (Equity) : $100711.65
P&L Neto (Ganancia)    : $711.65 (0.71%)
Operaciones ejecutadas : 25
Comisiones pagadas     : $214.27
Pérdida por Slippage   : $107.14
Costo total de fricción: $321.41
Posición remanente     : 0.50 BTC
------------------------------------------
Max Drawdown (Caída)   : 0.24%
Sharpe Ratio (Anual)   : 182.666
==========================================

**Notas de interpretación**

- El P&L es neto de comisiones y slippage. La fricción total ($321.41) equivale a ~45% del P&L neto, por lo que el resultado es sensible a los costos de transacción.
- La posición remanente de 0.50 BTC permanece abierta al cierre del dataset y se valora a mercado.
- El Sharpe anualizado se calcula sobre retornos de muy alta frecuencia en una única sesión de 24 h. Su magnitud refleja esa escala de muestreo y no debe extrapolarse como expectativa de rendimiento. La validación fuera de muestra (otros días y regímenes de mercado) queda pendiente.

---

## 5. Estructura del repositorio

```
arena-allocator/
├── Arena.hpp          # ArenaAllocator: bump allocator, alineación, reset O(1)
├── FastFileReader.hpp # mmap + madvise, parse_uint64 / parse_double
├── MarketData.hpp     # Tick y decodificación de trades
├── Portfolio.hpp      # Posición, fricción, PnL, drawdown, Sharpe
├── Engine.hpp         # Estrategia Momentum Breakout y bucle principal
├── main.cpp           # Punto de entrada, benchmark y reporte
└── README.md
```

---

## 6. Compilación y ejecución

**Requisitos:** macOS en Apple Silicon (ARM64) y Apple Clang con soporte C++20 (Xcode Command Line Tools).

```bash

# 1. Compilar
clang++ -std=c++20 -O3 -Wall -Wextra main.cpp -o arena_backtest

# 2. Ejecutar
./arena_backtest
```
---
## 7. Licencia 

MIT
EOF

