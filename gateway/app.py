import asyncio
import itertools
import os
import random
import time
from contextlib import asynccontextmanager
from pathlib import Path

import httpx
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, Response
from pydantic import BaseModel

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ENGINE_PATH = REPO_ROOT / "orderbook" / "build" / "engine_server"
WEBSITE_ORIGIN = "http://localhost:3000"

engine_processes: list[asyncio.subprocess.Process] = []
next_port = itertools.count(6060)
order_ids = itertools.count(1)
symbol_ports: dict[str, int] = {}
engine_tasks: dict[str, asyncio.Task[str]] = {}
engine_client: httpx.AsyncClient | None = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    global engine_client
    engine_client = httpx.AsyncClient(timeout=10)
    yield
    for proc in engine_processes:
        if proc.returncode is None:
            proc.kill()
    for proc in engine_processes:
        await proc.wait()
    engine_processes.clear()
    symbol_ports.clear()
    engine_tasks.clear()
    if engine_client is not None:
        await engine_client.aclose()
        engine_client = None


def http() -> httpx.AsyncClient:
    if engine_client is None:
        raise RuntimeError("HTTP client is not started")
    return engine_client


async def spawn(symbol: str, port: int) -> str:
    proc = await asyncio.create_subprocess_exec(
        str(engine_path()),
        str(port),
        stdout=asyncio.subprocess.DEVNULL,
        stderr=asyncio.subprocess.DEVNULL,
    )
    engine_processes.append(proc)
    url = f"http://127.0.0.1:{port}"
    for _ in range(50):
        if proc.returncode is not None:
            break
        try:
            response = await http().get(f"{url}/status")
            if response.status_code == 200:
                return url
        except httpx.TransportError:
            pass
        await asyncio.sleep(0.1)
    if proc.returncode is None:
        proc.kill()
    raise RuntimeError(f"engine for {symbol} on port {port} never became ready")


async def engine_url(symbol: str) -> str:
    # Check and store the task before any await, so a second caller for the same
    # symbol waits on the startup already in progress. Different symbols still start together.
    task = engine_tasks.get(symbol)
    if task is None:
        port = next(next_port)
        symbol_ports[symbol] = port
        task = engine_tasks[symbol] = asyncio.create_task(spawn(symbol, port))
    try:
        return await task
    except Exception:
        if engine_tasks.get(symbol) is task:
            engine_tasks.pop(symbol, None)
            symbol_ports.pop(symbol, None)
        raise


app = FastAPI(lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=[WEBSITE_ORIGIN],
    allow_methods=["*"],
    allow_headers=["*"],
)


class StockSimConfig(BaseModel):
    symbol: str
    numBids: int
    numAsks: int
    priceMin: int
    priceMax: int
    quantityMin: int
    quantityMax: int


class SimulationRequest(BaseModel):
    stocks: list[StockSimConfig]


class TradeRequest(BaseModel):
    tradetype: str
    side: str
    price: int
    quantity: int
    name: str  # symbol. The engine calls this field "book".


class CancelRequest(BaseModel):
    orderID: int
    name: str


def validate_simulation(request: SimulationRequest) -> None:
    if not request.stocks:
        raise HTTPException(status_code=400, detail="no stocks provided in simulation request")
    for stock in request.stocks:
        if stock.symbol.strip() == "":
            raise HTTPException(status_code=400, detail="stock symbol cannot be empty")
        if stock.priceMin > stock.priceMax:
            raise HTTPException(
                status_code=400, detail=f"priceMin cannot be greater than priceMax for {stock.symbol}"
            )
        if stock.quantityMin > stock.quantityMax:
            raise HTTPException(
                status_code=400,
                detail=f"quantityMin cannot be greater than quantityMax for {stock.symbol}",
            )
        if stock.numBids < 0 or stock.numAsks < 0:
            raise HTTPException(status_code=400, detail=f"order counts cannot be negative for {stock.symbol}")


def make_orders(stock: StockSimConfig) -> list[dict]:
    orders = []
    for side, count in (("BUY", stock.numBids), ("SELL", stock.numAsks)):
        for _ in range(count):
            orders.append(
                {
                    "orderid": next(order_ids),
                    "book": stock.symbol,
                    "tradetype": "GTC",
                    "side": side,
                    "price": random.randint(stock.priceMin, stock.priceMax),
                    "quantity": random.randint(stock.quantityMin, stock.quantityMax),
                }
            )
    return orders


async def post_batch(url: str, orders: list[dict]) -> dict:
    response = await http().post(f"{url}/batch", json={"orders": orders})
    if response.status_code >= 400:
        raise HTTPException(status_code=502, detail=response.text)
    return response.json()


def stock_result(symbol: str, payload: dict | None) -> dict:
    book = {}
    if payload is not None:
        book = payload.get("results", {}).get(symbol, {})
    return {
        "symbol": symbol,
        "tradesExecuted": book.get("tradesExecuted", 0),
        "volumeTraded": book.get("volumeTraded", 0),
        "remainingBids": book.get("remainingBids", 0),
        "remainingAsks": book.get("remainingAsks", 0),
        "bestBidPrice": book.get("bestBidPrice"),
        "bestAskPrice": book.get("bestAskPrice"),
        "bidLevels": book.get("bidLevels", 0),
        "askLevels": book.get("askLevels", 0),
    }


@app.post("/order/simulation")
async def simulation(request: SimulationRequest):
    validate_simulation(request)

    urls = await asyncio.gather(*(engine_url(stock.symbol) for stock in request.stocks))
    await asyncio.gather(*(http().post(f"{url}/reset") for url in dict.fromkeys(urls)))

    orders_by_symbol: dict[str, list[dict]] = {}
    for stock in request.stocks:
        orders_by_symbol.setdefault(stock.symbol, []).extend(make_orders(stock))

    symbol_to_url: dict[str, str] = {}
    for stock, url in zip(request.stocks, urls):
        symbol_to_url[stock.symbol] = url

    # Time only the matching round trip. Engine startup is excluded.
    started = time.perf_counter()
    jobs = [(symbol, url) for symbol, url in symbol_to_url.items() if orders_by_symbol[symbol]]
    batches = await asyncio.gather(*(post_batch(url, orders_by_symbol[symbol]) for symbol, url in jobs))
    elapsed_ms = (time.perf_counter() - started) * 1000

    payload_by_symbol = {symbol: payload for (symbol, _url), payload in zip(jobs, batches)}
    results = [stock_result(stock.symbol, payload_by_symbol.get(stock.symbol)) for stock in request.stocks]
    return {
        "executionTimeMs": round(elapsed_ms, 2),
        "totalOrdersProcessed": sum(payload.get("processedCount", 0) for payload in batches),
        "results": results,
    }


async def forward_form(url: str, path: str, fields: dict) -> Response:
    response = await http().post(f"{url}{path}", data=fields)
    return Response(content=response.content, status_code=response.status_code, media_type="application/json")


@app.post("/order/trade")
async def trade(request: TradeRequest):
    if request.name.strip() == "":
        raise HTTPException(status_code=400, detail="stock symbol cannot be empty")
    order_id = next(order_ids)
    url = await engine_url(request.name)
    response = await forward_form(
        url,
        "/trade",
        {
            "orderid": str(order_id),
            "tradetype": request.tradetype,
            "side": request.side,
            "price": str(request.price),
            "quantity": str(request.quantity),
            "book": request.name,
        },
    )
    if response.status_code == 200:
        return JSONResponse({"message": "Order placed successfully", "orderid": order_id})
    return response


@app.post("/order/cancel")
async def cancel(request: CancelRequest):
    if request.orderID == 0:
        raise HTTPException(status_code=400, detail="orderId field is required, and cannot be zero")
    if request.name.strip() == "":
        raise HTTPException(status_code=400, detail="stock symbol cannot be empty")
    url = await engine_url(request.name)
    return await forward_form(url, "/cancel", {"orderid": str(request.orderID), "book": request.name})


@app.post("/order/reset")
async def reset_all():
    urls = [f"http://127.0.0.1:{port}" for port in symbol_ports.values()]
    if not urls:
        return {"message": "All orderbooks cleared", "booksCleared": 0}
    responses = await asyncio.gather(*(http().post(f"{url}/reset") for url in urls))
    cleared = 0
    for response in responses:
        if response.status_code == 200:
            cleared += response.json().get("booksCleared", 0)
    return {"message": "All orderbooks cleared", "booksCleared": cleared}


@app.get("/order/status")
async def status():
    async def one(symbol: str, port: int) -> tuple[str, dict]:
        response = await http().get(f"http://127.0.0.1:{port}/status")
        if response.status_code != 200:
            return symbol, {}
        return symbol, response.json()

    pairs = await asyncio.gather(*(one(symbol, port) for symbol, port in symbol_ports.items()))
    return {symbol: body for symbol, body in pairs}


async def engine_rows() -> list[dict]:
    async def one(symbol: str, port: int) -> dict:
        url = f"http://127.0.0.1:{port}"
        healthy = False
        try:
            response = await http().get(f"{url}/status")
            healthy = response.status_code == 200
        except httpx.TransportError:
            healthy = False
        return {"symbol": symbol, "port": port, "healthy": healthy, "url": url}

    return list(await asyncio.gather(*(one(symbol, port) for symbol, port in symbol_ports.items())))


@app.get("/order/health")
async def health():
    engines = await engine_rows()
    healthy_count = sum(1 for engine in engines if engine["healthy"])
    if not engines or healthy_count == len(engines):
        status_name = "healthy"
    elif healthy_count == 0:
        status_name = "unhealthy"
    else:
        status_name = "degraded"
    return {
        "status": status_name,
        "totalEngines": len(engines),
        "healthyEngines": healthy_count,
        "engines": engines,
    }


@app.get("/order/engines")
async def engines():
    rows = await engine_rows()
    return {"count": len(symbol_ports), "mapping": dict(symbol_ports), "engines": rows}


def engine_path() -> Path:
    configured = os.environ.get("ENGINE_PATH")
    if configured:
        return Path(configured)
    return DEFAULT_ENGINE_PATH


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=8000)
