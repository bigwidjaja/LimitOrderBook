export type StockDraft = {
  id: string;
  symbol: string;
  numBids: string;
  numAsks: string;
  priceMin: string;
  priceMax: string;
  quantityMin: string;
  quantityMax: string;
};

export type StockRequest = {
  symbol: string;
  numBids: number;
  numAsks: number;
  priceMin: number;
  priceMax: number;
  quantityMin: number;
  quantityMax: number;
};

export type StockResult = {
  symbol: string;
  tradesExecuted: number;
  volumeTraded: number;
  remainingBids: number;
  remainingAsks: number;
  bestBidPrice: number | null;
  bestAskPrice: number | null;
  bidLevels: number;
  askLevels: number;
};

export type SimulationResponse = {
  executionTimeMs: number;
  totalOrdersProcessed: number;
  results: StockResult[];
};

export function newDraft(symbol = "", id = ""): StockDraft {
  return {
    id: id || crypto.randomUUID(),
    symbol,
    numBids: "100",
    numAsks: "100",
    priceMin: "100",
    priceMax: "200",
    quantityMin: "10",
    quantityMax: "100",
  };
}
