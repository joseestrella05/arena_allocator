import urllib.request
import json
import csv
import sys

# Descargaremos transacciones reales de Bitcoin (BTCUSDT)
SYMBOL = "BTCUSDT"
LIMIT = 1000  # Máximo por llamada en la API pública de trades
TOTAL_BATCHES = 50 # 50 llamadas = ~50,000 operaciones reales consecutivas
OUTPUT_FILE = "real_ticks.csv"

print(f"Descargando operaciones reales de mercado para {SYMBOL}...")

all_trades = []
last_id = None

for i in range(TOTAL_BATCHES):
    url = f"https://api.binance.com/api/v3/historicalTrades?symbol={SYMBOL}&limit={LIMIT}"
    if last_id is not None:
        url += f"&fromId={last_id}"
    
    req = urllib.request.Request(
        url, 
        headers={'User-Agent': 'Mozilla/5.0'}
    )
    
    try:
        with urllib.request.urlopen(req) as response:
            data = json.loads(response.read().decode())
            if not data:
                break
            all_trades.extend(data)
            last_id = data[-1]['id'] + 1
            print(f"Lote {i+1}/{TOTAL_BATCHES} descargado ({len(all_trades)} trades acumulados)...")
    except Exception as e:
        print(f"Error en la descarga: {e}")
        break

if not all_trades:
    print("No se pudieron obtener datos. Revisa la conexión.")
    sys.exit(1)

print(f"Guardando {len(all_trades)} registros en {OUTPUT_FILE}...")

with open(OUTPUT_FILE, mode="w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["timestamp", "symbol", "price", "volume", "type"])
    for t in all_trades:
        # Convertir milisegundos a nanosegundos (multiplicar por 1,000,000)
        ts_ns = int(t['time']) * 1_000_000
        price = float(t['price'])
        vol = int(float(t['qty']) * 1000) # Normalizado
        writer.writerow([ts_ns, "BTC", f"{price:.2f}", vol, "TRADE"])

print("Descarga y formateo completados exitosamente.")
