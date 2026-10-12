"use client";

import { useState } from "react";
import { GatewayError, runSimulation } from "@/lib/api";
import { newDraft, type SimulationResponse, type StockDraft, type StockRequest } from "@/lib/types";
import { BookPanel } from "./BookPanel";
import { OrderBar } from "./OrderBar";
import { SymbolCard } from "./SymbolCard";

function wholeNumber(value: string): number | null {
  if (!/^-?\d+$/.test(value.trim())) return null;
  return Number(value);
}

function problemsFor(drafts: StockDraft[]): Record<string, string> {
  const problems: Record<string, string> = {};
  const seen = new Map<string, string>();
  for (const draft of drafts) {
    const symbol = draft.symbol.trim().toUpperCase();
    if (!symbol) {
      problems[draft.id] = "Enter a symbol.";
      continue;
    }
    const previous = seen.get(symbol);
    if (previous) {
      problems[draft.id] = `${symbol} is already on the desk.`;
      problems[previous] = `${symbol} is already on the desk.`;
    }
    seen.set(symbol, draft.id);

    const numBids = wholeNumber(draft.numBids);
    const numAsks = wholeNumber(draft.numAsks);
    const priceMin = wholeNumber(draft.priceMin);
    const priceMax = wholeNumber(draft.priceMax);
    const quantityMin = wholeNumber(draft.quantityMin);
    const quantityMax = wholeNumber(draft.quantityMax);
    if (
      numBids === null ||
      numAsks === null ||
      priceMin === null ||
      priceMax === null ||
      quantityMin === null ||
      quantityMax === null
    ) {
      problems[draft.id] = "Every count and range needs a whole number.";
      continue;
    }
    if (numBids < 0 || numAsks < 0) problems[draft.id] = "Order counts cannot be negative.";
    else if (numBids + numAsks === 0) problems[draft.id] = "Add at least one bid or ask.";
    else if (priceMin > priceMax) problems[draft.id] = "Price min is above price max.";
    else if (quantityMin > quantityMax) problems[draft.id] = "Quantity min is above quantity max.";
    else if (quantityMin < 0) problems[draft.id] = "Quantity cannot be negative.";
  }
  return problems;
}

function toRequest(draft: StockDraft): StockRequest {
  return {
    symbol: draft.symbol.trim().toUpperCase(),
    numBids: Number(draft.numBids),
    numAsks: Number(draft.numAsks),
    priceMin: Number(draft.priceMin),
    priceMax: Number(draft.priceMax),
    quantityMin: Number(draft.quantityMin),
    quantityMax: Number(draft.quantityMax),
  };
}

export function Desk() {
  const [drafts, setDrafts] = useState<StockDraft[]>([newDraft("TSLA", "tsla")]);
  const [fieldErrors, setFieldErrors] = useState<Record<string, string>>({});
  const [gatewayError, setGatewayError] = useState<string | null>(null);
  const [results, setResults] = useState<SimulationResponse | null>(null);
  const [running, setRunning] = useState(false);

  function update(next: StockDraft) {
    setDrafts((current) => current.map((draft) => (draft.id === next.id ? next : draft)));
  }

  async function onRun() {
    const problems = problemsFor(drafts);
    setFieldErrors(problems);
    if (Object.keys(problems).length > 0) return;

    setRunning(true);
    setGatewayError(null);
    try {
      const response = await runSimulation(drafts.map(toRequest));
      setResults(response);
    } catch (error) {
      setGatewayError(error instanceof GatewayError ? error.message : "The simulation did not finish.");
    } finally {
      setRunning(false);
    }
  }

  const ordersPerSecond =
    results && results.executionTimeMs > 0
      ? Math.round(results.totalOrdersProcessed / (results.executionTimeMs / 1000))
      : null;

  return (
    <div className="min-h-screen">
      <header className="border-b border-line">
        <div className="mx-auto flex max-w-6xl items-end justify-between px-6 py-5">
          <div>
            <p className="text-[11px] uppercase tracking-[0.2em] text-accent">Limit book</p>
            <h1 className="mt-1 text-2xl font-medium">Simulation desk</h1>
          </div>
          <p className="text-xs text-muted">Gateway on port 8000</p>
        </div>
      </header>

      <main className="mx-auto flex max-w-6xl flex-col gap-6 px-6 py-6">
        <OrderBar />

        <section>
          <div className="mb-3 flex items-center justify-between">
            <h2 className="text-sm font-medium">Symbols</h2>
            <button
              type="button"
              onClick={() => setDrafts((current) => [...current, newDraft()])}
              className="rounded-md border border-line px-3 py-1.5 text-sm text-ink hover:border-accent"
            >
              Add symbol
            </button>
          </div>
          <div className="grid gap-4 md:grid-cols-2">
            {drafts.map((draft) => (
              <SymbolCard
                key={draft.id}
                draft={draft}
                error={fieldErrors[draft.id]}
                canRemove={drafts.length > 1}
                onChange={update}
                onRemove={() => setDrafts((current) => current.filter((item) => item.id !== draft.id))}
              />
            ))}
          </div>
          <div className="mt-4 flex flex-wrap items-center gap-3">
            <button
              type="button"
              onClick={onRun}
              disabled={running}
              aria-busy={running}
              className="rounded-md bg-ink px-4 py-2 text-sm font-medium text-bg disabled:opacity-50"
            >
              {running ? "Running…" : "Run simulation"}
            </button>
            {gatewayError ? <p className="text-sm text-ask">{gatewayError}</p> : null}
          </div>
        </section>

        <section>
          {results ? (
            <>
              <div className="mb-3 flex flex-wrap items-baseline justify-between gap-2">
                <h2 className="text-sm font-medium">Books</h2>
                <p className="font-mono text-xs text-muted">
                  {results.executionTimeMs.toFixed(2)} ms · {results.totalOrdersProcessed.toLocaleString()} orders
                  {ordersPerSecond !== null ? ` · ${ordersPerSecond.toLocaleString()}/s` : ""} · {results.results.length}{" "}
                  {results.results.length === 1 ? "engine" : "engines"}
                </p>
              </div>
              <div className="grid gap-4 md:grid-cols-2">
                {results.results.map((result) => (
                  <BookPanel key={result.symbol} result={result} />
                ))}
              </div>
            </>
          ) : (
            <div className="rounded-xl border border-dashed border-line px-6 py-16 text-center">
              <p className="text-lg">No book yet</p>
              <p className="mt-2 text-sm text-muted">Run a simulation to see the best bid, best ask, and what is still resting.</p>
            </div>
          )}
        </section>
      </main>
    </div>
  );
}
