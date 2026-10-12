from fastapi.testclient import TestClient

import app


def stock(symbol, bids=2, asks=2, price_min=100, price_max=110, quantity_min=1, quantity_max=4):
    return {
        "symbol": symbol,
        "numBids": bids,
        "numAsks": asks,
        "priceMin": price_min,
        "priceMax": price_max,
        "quantityMin": quantity_min,
        "quantityMax": quantity_max,
    }


def test_simulation_of_one_symbol_returns_a_result():
    with TestClient(app.app) as client:
        response = client.post("/order/simulation", json={"stocks": [stock("AAPL")]})
        assert response.status_code == 200
        body = response.json()
        assert "executionTimeMs" in body
        assert body["totalOrdersProcessed"] == 4
        assert len(body["results"]) == 1
        assert body["results"][0]["symbol"] == "AAPL"


def test_two_symbols_use_two_ports():
    with TestClient(app.app) as client:
        response = client.post("/order/simulation", json={"stocks": [stock("AAPL"), stock("GOOG")]})
        assert response.status_code == 200
        assert len(response.json()["results"]) == 2
        assert app.symbol_ports["AAPL"] != app.symbol_ports["GOOG"]
        assert len(app.engine_processes) == 2


def test_second_simulation_reuses_the_process():
    with TestClient(app.app) as client:
        client.post("/order/simulation", json={"stocks": [stock("AAPL")]})
        pid = app.engine_processes[0].pid
        count = len(app.engine_processes)
        response = client.post("/order/simulation", json={"stocks": [stock("AAPL")]})
        assert response.status_code == 200
        assert len(app.engine_processes) == count
        assert app.engine_processes[0].pid == pid


def test_price_min_greater_than_price_max_is_rejected():
    with TestClient(app.app) as client:
        response = client.post(
            "/order/simulation",
            json={"stocks": [stock("AAPL", price_min=20, price_max=10)]},
        )
        assert response.status_code == 400


def test_trade_then_cancel():
    with TestClient(app.app) as client:
        trade = client.post(
            "/order/trade",
            json={"tradetype": "GTC", "side": "BUY", "price": 100, "quantity": 5, "name": "MSFT"},
        )
        assert trade.status_code == 200
        order_id = trade.json()["orderid"]
        cancel = client.post("/order/cancel", json={"orderID": order_id, "name": "MSFT"})
        assert cancel.status_code == 200
        missing = client.post("/order/cancel", json={"orderID": order_id, "name": "MSFT"})
        assert missing.status_code == 404
