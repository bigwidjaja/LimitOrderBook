# Limit Order Book

A simulator for a central limit order book. The matching rules live in a C++ library. A small C++ program serves one book over HTTP. A FastAPI gateway starts one of those programs per stock, on this machine, and a Next.js page runs the simulation.

## Quick Start

Build the C++ engine, then start the gateway and the website:

```bash
cmake -S orderbook -B orderbook/build -DCMAKE_BUILD_TYPE=Debug
cmake --build orderbook/build --target engine_server

cd gateway && .venv/bin/python app.py
cd frontend && npm run dev
```

Opens:

- **Frontend**: http://localhost:3000
- **API**: http://localhost:8000
- **C++ Engines**: Spawned dynamically on ports 6060+

If `gateway/.venv` does not exist yet:

```bash
cd gateway
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

The gateway looks for the engine at `orderbook/build/engine_server`. Set `ENGINE_PATH` to use a different binary.

## Architecture

```text
Frontend (Next.js) → FastAPI gateway → Multiple C++ Engines (1 per stock)
```

When you run a simulation with 4 stocks, the gateway starts 4 `engine_server` processes and sends each stock's orders to its own process. The website talks only to the gateway. The gateway assigns order ids, starts an engine per symbol, and forwards that symbol's orders to that engine. The engine calls the order book library and returns JSON.

## API Endpoints

| Method | Endpoint | Description |
| --- | --- | --- |
| POST | `/order/trade` | Place an order |
| POST | `/order/cancel` | Cancel an order |
| POST | `/order/simulation` | Run a simulation |
| POST | `/order/reset` | Reset all engines |
| GET | `/order/status` | Get order book state |
| GET | `/order/health` | Check engine health |
| GET | `/order/engines` | List running engines |

## Examples

**Place Order:**

```bash
curl -X POST http://localhost:8000/order/trade \
  -H "Content-Type: application/json" \
  -d '{"tradetype":"GTC","side":"BUY","price":100,"quantity":50,"name":"AAPL"}'
```

**Run Simulation:**

```bash
curl -X POST http://localhost:8000/order/simulation \
  -H "Content-Type: application/json" \
  -d '{"stocks":[{"symbol":"AAPL","numBids":100,"numAsks":100,"priceMin":100,"priceMax":200,"quantityMin":10,"quantityMax":100}]}'
```

**Check Health:**

```bash
curl http://localhost:8000/order/health
```

## Tech Stack

- **Frontend**: Next.js, React, Tailwind CSS
- **API Gateway**: Python (FastAPI, Uvicorn)
- **Matching Engine**: C++ (cpp-httplib, nlohmann/json)
