"use client";

import type { StockDraft } from "@/lib/types";

type Props = {
  draft: StockDraft;
  error?: string;
  onChange: (draft: StockDraft) => void;
  onRemove: () => void;
  canRemove: boolean;
};

function Field({
  label,
  value,
  onChange,
}: {
  label: string;
  value: string;
  onChange: (value: string) => void;
}) {
  return (
    <label className="block">
      <span className="mb-1 block text-[11px] uppercase tracking-wide text-muted">{label}</span>
      <input
        value={value}
        inputMode="numeric"
        onChange={(event) => onChange(event.target.value)}
        className="w-full rounded-md border border-line bg-bg px-2.5 py-2 font-mono text-sm text-ink outline-none focus:border-accent"
      />
    </label>
  );
}

export function SymbolCard({ draft, error, onChange, onRemove, canRemove }: Props) {
  const set = (field: keyof StockDraft, value: string) => onChange({ ...draft, [field]: value });

  return (
    <article className="rounded-xl border border-line bg-panel p-4">
      <div className="mb-4 flex items-start justify-between gap-3">
        <label className="min-w-0 flex-1">
          <span className="mb-1 block text-[11px] uppercase tracking-wide text-muted">Symbol</span>
          <input
            value={draft.symbol}
            onChange={(event) => set("symbol", event.target.value.toUpperCase())}
            className="w-full bg-transparent font-mono text-2xl tracking-wide text-ink outline-none placeholder:text-line"
            placeholder="AAPL"
          />
        </label>
        <button
          type="button"
          onClick={onRemove}
          disabled={!canRemove}
          className="rounded-md px-2 py-1 text-xs text-muted hover:text-ink disabled:opacity-30"
        >
          Remove
        </button>
      </div>
      <div className="grid grid-cols-2 gap-3">
        <Field label="Bids" value={draft.numBids} onChange={(value) => set("numBids", value)} />
        <Field label="Asks" value={draft.numAsks} onChange={(value) => set("numAsks", value)} />
        <Field label="Price min" value={draft.priceMin} onChange={(value) => set("priceMin", value)} />
        <Field label="Price max" value={draft.priceMax} onChange={(value) => set("priceMax", value)} />
        <Field label="Qty min" value={draft.quantityMin} onChange={(value) => set("quantityMin", value)} />
        <Field label="Qty max" value={draft.quantityMax} onChange={(value) => set("quantityMax", value)} />
      </div>
      {error ? <p className="mt-3 text-sm text-ask">{error}</p> : null}
    </article>
  );
}
