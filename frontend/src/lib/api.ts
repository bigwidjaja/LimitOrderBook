import type { SimulationResponse, StockRequest } from "./types";

const API = "http://localhost:8000/order";

export class GatewayError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "GatewayError";
  }
}

async function readError(response: Response): Promise<string> {
  const data: unknown = await response.json().catch(() => null);
  if (data && typeof data === "object" && "detail" in data) {
    const detail = (data as { detail: unknown }).detail;
    if (typeof detail === "string") return detail;
  }
  return `The gateway returned ${response.status}.`;
}

async function postJson<T>(path: string, body: unknown): Promise<T> {
  let response: Response;
  try {
    response = await fetch(`${API}${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
  } catch {
    throw new GatewayError("The gateway on port 8000 is not reachable.");
  }
  if (!response.ok) throw new GatewayError(await readError(response));
  return response.json() as Promise<T>;
}

export function runSimulation(stocks: StockRequest[]) {
  return postJson<SimulationResponse>("/simulation", { stocks });
}

export function placeOrder(order: {
  name: string;
  side: "BUY" | "SELL";
  tradetype: "GTC" | "FAK";
  price: number;
  quantity: number;
}) {
  return postJson<{ message: string; orderid: number }>("/trade", order);
}

export function cancelOrder(name: string, orderID: number) {
  return postJson<{ message: string }>("/cancel", { name, orderID });
}
