import type { StockResult } from "@/lib/types";

function price(value: number | null) {
  return value === null ? "—" : value.toLocaleString();
}

export function BookPanel({ result }: { result: StockResult }) {
  const spread =
    result.bestBidPrice !== null && result.bestAskPrice !== null
      ? result.bestAskPrice - result.bestBidPrice
      : null;

  return (
    <article className="rounded-xl border border-line bg-panel p-5">
      <header className="mb-4 flex items-baseline justify-between">
        <h3 className="font-mono text-xl tracking-wide">{result.symbol}</h3>
        <p className="text-xs text-muted">
          {result.bidLevels} bid {result.bidLevels === 1 ? "level" : "levels"} · {result.askLevels} ask{" "}
          {result.askLevels === 1 ? "level" : "levels"}
        </p>
      </header>
      <div className="grid grid-cols-2 gap-4 border-y border-line py-4">
        <div>
          <p className="text-[11px] uppercase tracking-wide text-bid">Best bid</p>
          <p className="mt-1 font-mono text-4xl text-bid">{price(result.bestBidPrice)}</p>
          <p className="mt-1 text-sm text-muted">{result.remainingBids.toLocaleString()} resting</p>
        </div>
        <div className="text-right">
          <p className="text-[11px] uppercase tracking-wide text-ask">Best ask</p>
          <p className="mt-1 font-mono text-4xl text-ask">{price(result.bestAskPrice)}</p>
          <p className="mt-1 text-sm text-muted">{result.remainingAsks.toLocaleString()} resting</p>
        </div>
      </div>
      <p className="mt-3 font-mono text-sm text-muted">
        Spread <span className="text-ink">{spread === null ? "—" : spread.toLocaleString()}</span>
      </p>
      <dl className="mt-4 grid grid-cols-2 gap-3 text-sm">
        <div>
          <dt className="text-muted">Trades</dt>
          <dd className="font-mono">{result.tradesExecuted.toLocaleString()}</dd>
        </div>
        <div>
          <dt className="text-muted">Volume</dt>
          <dd className="font-mono">{result.volumeTraded.toLocaleString()}</dd>
        </div>
      </dl>
    </article>
  );
}
