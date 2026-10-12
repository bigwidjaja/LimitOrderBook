'''
note to self on how this works: 
1. engine_server built 
2. uvicorn starts which loads the fastapi app and waits on port 8000 
3. website is started on port 3000 
4. you submit a simulation and the website sends the form to gateway on port8000 
5. gateway starts one engine_server per stock, each on its own port 
6. gateway waits until each engine answers /status 
7. gateway resets each book, creates the orders, and assigns ids. 
8. gateway sends each stock's order to that stock's port 
9. engine calls the library, which matches the orders and updates the book 
10. engine sends json back to the gateway 
11. gateway combines replies into one response and the website shows that response
''' 
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

# Processes this gateway starts. Shutdown kills each one.
engine_processes: list[asyncio.subprocess.Process] = []
# symbol -> port, and symbol -> the one startup already in progress.
next_port = itertools.count(6060)
order_ids = itertools.count(1)
symbol_ports: dict[str, int] = {}
engine_tasks: dict[str, asyncio.Task[str]] = {}
# One client for every call to an engine. Created at startup, closed at shutdown.
engine_client: httpx.AsyncClient | None = None


# FastAPI calls this when the app starts and when it stops.
# Code above yield would run at startup. Code below yield runs at shutdown.
# async means this function can pause. @asynccontextmanager lets yield split startup from shutdown.
@asynccontextmanager # review, this is a decorator. wraps the function written under it (runs the same thing as lifespan = asynccontextmanager(lifespan)
async def lifespan(app: FastAPI):
    global engine_client
    engine_client = httpx.AsyncClient(timeout=10)  # talks to 127.0.0.1:<port>, not to the book
    yield  # app serves requests while paused here
    for proc in engine_processes:
        if proc.returncode is None:  # None means the process is still running
            proc.kill()  # ask the OS to stop it
    for proc in engine_processes:
        await proc.wait()  # pause until that process has actually exited
    engine_processes.clear()  # drop the finished processes from the list
    symbol_ports.clear()
    engine_tasks.clear()
    if engine_client is not None:
        await engine_client.aclose()
        engine_client = None


# The shared client. Raises if a route calls an engine before startup created it.
def http() -> httpx.AsyncClient:
    if engine_client is None:
        raise RuntimeError("HTTP client is not started")
    return engine_client


# Starts one engine_server and returns its URL once GET /status is 200.
async def spawn(symbol: str, port: int) -> str:
    # create_subprocess_exec runs the binary. DEVNULL throws away its printed logs.
    proc = await asyncio.create_subprocess_exec(
        str(engine_path()),
        str(port),
        stdout=asyncio.subprocess.DEVNULL,
        stderr=asyncio.subprocess.DEVNULL,
    )
    engine_processes.append(proc)
    url = f"http://127.0.0.1:{port}"
    for _ in range(50):  # about 5 seconds
        if proc.returncode is not None:
            break
        try:
            response = await http().get(f"{url}/status")
            if response.status_code == 200:
                return url
        except httpx.TransportError:
            pass  # engine is not accepting connections yet
        await asyncio.sleep(0.1)
    if proc.returncode is None:
        proc.kill()
    raise RuntimeError(f"engine for {symbol} on port {port} never became ready")


async def engine_url(symbol: str) -> str:
    # No await between the check and the save, so two callers for AAPL share one task.
    # AAPL and GOOG each get their own task, so those two starts run together.
    task = engine_tasks.get(symbol)
    if task is None:
        port = next(next_port)
        symbol_ports[symbol] = port
        # create_task starts spawn without waiting here, so another symbol can start too.
        task = engine_tasks[symbol] = asyncio.create_task(spawn(symbol, port))
    try:
        return await task
    except Exception:
        if engine_tasks.get(symbol) is task:
            engine_tasks.pop(symbol, None)
            symbol_ports.pop(symbol, None)
        raise


# Creates the web app and tells it to run lifespan on startup and shutdown.
app = FastAPI(lifespan=lifespan)
# Middleware runs on every request before the route. This one adds the CORS headers.
app.add_middleware(
    CORSMiddleware,
    allow_origins=[WEBSITE_ORIGIN],  # only the page at localhost:3000 may read replies
    allow_methods=["*"],  # * means every method: GET, POST, and the rest
    allow_headers=["*"],  # * means the browser may send any request header
)


# BaseModel is the shape of the JSON body. FastAPI rejects a request that doesn't match.
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
    name: str


class CancelRequest(BaseModel):
    orderID: int
    name: str


# Bad input becomes HTTP 400. detail is the error text in the JSON reply.
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
    # Bids first, then asks, matching the Go loops. Ids come from this gateway.
    orders = []
    for side, count in (("BUY", stock.numBids), ("SELL", stock.numAsks)):
        for _ in range(count):
            orders.append(
                {
                    "orderid": next(order_ids),
                    "book": stock.symbol,
                    "tradetype": "GTC",
                    "side": side,
                    # randint includes both ends, so 100 and 110 are both possible.
                    "price": random.randint(stock.priceMin, stock.priceMax),
                    "quantity": random.randint(stock.quantityMin, stock.quantityMax),
                }
            )
    return orders


# json= turns the dict into a JSON body and sets Content-Type for us.
async def post_batch(url: str, orders: list[dict]) -> dict:
    response = await http().post(f"{url}/batch", json={"orders": orders})
    if response.status_code >= 400:
        raise HTTPException(status_code=502, detail=response.text)
    return response.json()


def stock_result(symbol: str, payload: dict | None) -> dict:
    book = {}
    if payload is not None:
        # .get(key, default) returns default when the key is missing. None stays JSON null.
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


# @app.post registers this function as the handler for that path.
@app.post("/order/simulation")
async def simulation(request: SimulationRequest):
    validate_simulation(request)

    # gather runs these at the same time and waits until all of them finish.
    urls = await asyncio.gather(*(engine_url(stock.symbol) for stock in request.stocks))
    # fromkeys drops duplicate URLs, so one symbol is reset once.
    await asyncio.gather(*(http().post(f"{url}/reset") for url in dict.fromkeys(urls)))

    orders_by_symbol: dict[str, list[dict]] = {}
    for stock in request.stocks:
        # setdefault creates the list on the first stock with that symbol.
        orders_by_symbol.setdefault(stock.symbol, []).extend(make_orders(stock))

    symbol_to_url: dict[str, str] = {}
    for stock, url in zip(request.stocks, urls):
        symbol_to_url[stock.symbol] = url

    # Clock starts after engines are up and books are reset. Startup is not included.
    started = time.perf_counter()  # seconds since an arbitrary start, used only as a difference
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


# data= sends form fields, which is what /trade and /cancel on the engine read.
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
        # JSONResponse is a reply whose body is this dict, turned into JSON.
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
    # {symbol: body, ...} builds a dict. The website sees one key per stock.
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


# -> Path means this function returns a Path.
def engine_path() -> Path:
    configured = os.environ.get("ENGINE_PATH")  # None if the variable was not set
    if configured:
        return Path(configured)  # turn that text into a path
    return DEFAULT_ENGINE_PATH


# True only when this file is run directly, not when tests import it.
if __name__ == "__main__":
    import uvicorn

    # host 0.0.0.0 accepts connections on this machine. port 8000 is the gateway.
    uvicorn.run(app, host="0.0.0.0", port=8000)
