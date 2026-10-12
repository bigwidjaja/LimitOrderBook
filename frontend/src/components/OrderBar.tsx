"use client";

import { useState } from "react";
import { GatewayError, cancelOrder, placeOrder } from "@/lib/api";

export function OrderBar() {
  const [symbol, setSymbol] = useState("TSLA");
  const [side, setSide] = useState<"BUY" | "SELL">("BUY");
  const [tradetype, setTradetype] = useState<"GTC" | "FAK">("GTC");
  const [price, setPrice] = useState("100");
  const [quantity, setQuantity] = useState("10");
  const [cancelId, setCancelId] = useState("");
  const [note, setNote] = useState<string | null>(null);
  const [noteIsError, setNoteIsError] = useState(false);
  const [busy, setBusy] = useState(false);

  function report(message: string, isError: boolean) {
    setNote(message);
    setNoteIsError(isError);
  }

  async function onPlace() {
    const parsedPrice = Number(price);
    const parsedQuantity = Number(quantity);
    if (!symbol.trim() || !Number.isInteger(parsedPrice) || !Number.isInteger(parsedQuantity) || parsedQuantity <= 0) {
      report("A place needs a symbol, a whole price, and a quantity above 0.", true);
      return;
    }
    setBusy(true);
    try {
      const result = await placeOrder({
        name: symbol.trim().toUpperCase(),
        side,
        tradetype,
        price: parsedPrice,
        quantity: parsedQuantity,
      });
      setCancelId(String(result.orderid));
      report(`Placed order ${result.orderid}.`, false);
    } catch (error) {
      report(error instanceof GatewayError ? error.message : "The order was not placed.", true);
    } finally {
      setBusy(false);
    }
  }

  async function onCancel() {
    const parsedId = Number(cancelId);
    if (!symbol.trim() || !Number.isInteger(parsedId) || parsedId <= 0) {
      report("A cancel needs a symbol and an order id.", true);
      return;
    }
    setBusy(true);
    try {
      await cancelOrder(symbol.trim().toUpperCase(), parsedId);
      report(`Canceled order ${parsedId}.`, false);
    } catch (error) {
      report(error instanceof GatewayError ? error.message : "The order was not canceled.", true);
    } finally {
      setBusy(false);
    }
  }

  return (
    <section className="rounded-xl border border-line bg-panel px-4 py-4">
      <div className="mb-3 flex items-baseline justify-between gap-3">
        <h2 className="text-sm font-medium text-ink">One order</h2>
        <p className="text-xs text-muted">Places and cancels go to the gateway, then to that symbol&apos;s engine.</p>
      </div>
      <div className="grid gap-3 md:grid-cols-[8rem_1fr_1fr_6rem_6rem_auto]">
        <label className="block">
          <span className="mb-1 block text-[11px] uppercase tracking-wide text-muted">Symbol</span>
          <input
            value={symbol}
            onChange={(event) => setSymbol(event.target.value.toUpperCase())}
            className="w-full rounded-md border border-line bg-bg px-2.5 py-2 font-mono text-sm outline-none focus:border-accent"
          />
        </label>
        <div>
          <span className="mb-1 block text-[11px] uppercase tracking-wide text-muted">Side</span>
          <div className="grid grid-cols-2 rounded-md border border-line p-0.5">
            {(["BUY", "SELL"] as const).map((choice) => (
              <button
                key={choice}
                type="button"
                onClick={() => setSide(choice)}
                className={`rounded px-2 py-1.5 text-xs font-medium ${
                  side === choice ? (choice === "BUY" ? "bg-bid text-bg" : "bg-ask text-bg") : "text-muted"
                }`}
              >
                {choice === "BUY" ? "Buy" : "Sell"}
              </button>
            ))}
          </div>
        </div>
        <div>
          <span className="mb-1 block text-[11px] uppercase tracking-wide text-muted">Type</span>
          <div className="grid grid-cols-2 rounded-md border border-line p-0.5">
            {(["GTC", "FAK"] as const).map((choice) => (
              <button
                key={choice}
                type="button"
                onClick={() => setTradetype(choice)}
                className={`rounded px-2 py-1.5 text-xs ${tradetype === choice ? "bg-panel-2 text-ink" : "text-muted"}`}
              >
                {choice}
              </button>
            ))}
          </div>
        </div>
        <label className="block">
          <span className="mb-1 block text-[11px] uppercase tracking-wide text-muted">Price</span>
          <input
            value={price}
            inputMode="numeric"
            onChange={(event) => setPrice(event.target.value)}
            className="w-full rounded-md border border-line bg-bg px-2.5 py-2 font-mono text-sm outline-none focus:border-accent"
          />
        </label>
        <label className="block">
          <span className="mb-1 block text-[11px] uppercase tracking-wide text-muted">Qty</span>
          <input
            value={quantity}
            inputMode="numeric"
            onChange={(event) => setQuantity(event.target.value)}
            className="w-full rounded-md border border-line bg-bg px-2.5 py-2 font-mono text-sm outline-none focus:border-accent"
          />
        </label>
        <div className="flex items-end">
          <button
            type="button"
            onClick={onPlace}
            disabled={busy}
            className="w-full rounded-md bg-accent px-3 py-2 text-sm font-medium text-bg disabled:opacity-50"
          >
            Place
          </button>
        </div>
      </div>
      <div className="mt-3 flex flex-col gap-3 sm:flex-row sm:items-end">
        <label className="block sm:w-40">
          <span className="mb-1 block text-[11px] uppercase tracking-wide text-muted">Cancel id</span>
          <input
            value={cancelId}
            inputMode="numeric"
            onChange={(event) => setCancelId(event.target.value)}
            className="w-full rounded-md border border-line bg-bg px-2.5 py-2 font-mono text-sm outline-none focus:border-accent"
          />
        </label>
        <button
          type="button"
          onClick={onCancel}
          disabled={busy}
          className="rounded-md border border-line px-3 py-2 text-sm text-ink disabled:opacity-50"
        >
          Cancel
        </button>
        {note ? <p className={`text-sm sm:pb-2 ${noteIsError ? "text-ask" : "text-muted"}`}>{note}</p> : null}
      </div>
    </section>
  );
}
