#include "orderbook/orderbook.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

std::vector<Trade> Orderbook::add(Order order) {
    if (order.remaining == 0 || index_.contains(order.id)) {
        return {};
    }

    std::vector<Trade> trades;
    const bool isBuy = order.side == Side::Buy;

    // Best price first, then the oldest order at that price.
    // The trade is printed at the resting order's price.
    auto matchAgainst = [&](auto& levels) {
        while (order.remaining > 0 && !levels.empty()) {
            const Price best = levels.begin()->first;
            if (isBuy ? order.price < best : order.price > best) {
                break;
            }

            Level& level = levels.begin()->second;
            Order& resting = level.front();
            const Quantity quantity = std::min(order.remaining, resting.remaining);
            const Price tradePrice = resting.price;
            const OrderId restingId = resting.id;

            order.remaining -= quantity;
            resting.remaining -= quantity;
            if (isBuy) {
                trades.push_back(Trade{order.id, restingId, tradePrice, quantity});
            } else {
                trades.push_back(Trade{restingId, order.id, tradePrice, quantity});
            }

            // cancel() erases this node. The id has to be a copy, not a reference into it.
            if (resting.remaining == 0) {
                cancel(restingId);
            }
        }
    };

    if (isBuy) {
        matchAgainst(asks_);
    } else {
        matchAgainst(bids_);
    }

    // Fill-and-kill never rests. A good-till-cancel order rests only if quantity remains.
    if (order.type == OrderType::GoodTillCancel && order.remaining > 0) {
        if (isBuy) {
            Level& level = bids_[order.price];
            level.push_back(std::move(order));
            index_[level.back().id] = std::prev(level.end());
        } else {
            Level& level = asks_[order.price];
            level.push_back(std::move(order));
            index_[level.back().id] = std::prev(level.end());
        }
    }

    return trades;
}

bool Orderbook::cancel(OrderId id) {
    if (!index_.contains(id)) {
        return false;
    }

    Level::iterator node = index_[id];
    const Price price = node->price;
    const Side side = node->side;

    if (side == Side::Buy) {
        Level& level = bids_[price];
        level.erase(node);
        index_.erase(id);
        if (level.empty()) {
            bids_.erase(price);
        }
    } else {
        Level& level = asks_[price];
        level.erase(node);
        index_.erase(id);
        if (level.empty()) {
            asks_.erase(price);
        }
    }
    return true;
}

std::optional<Price> Orderbook::bestBid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> Orderbook::bestAsk() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

std::size_t Orderbook::size() const {
    return index_.size();
}

std::size_t Orderbook::bidCount() const {
    std::size_t count = 0;
    for (const auto& entry : bids_) {
        count += entry.second.size();
    }
    return count;
}

std::size_t Orderbook::askCount() const {
    std::size_t count = 0;
    for (const auto& entry : asks_) {
        count += entry.second.size();
    }
    return count;
}

std::size_t Orderbook::bidLevels() const {
    return bids_.size();
}

std::size_t Orderbook::askLevels() const {
    return asks_.size();
}

namespace {

template <class Levels>
std::vector<LevelSummary> summarize(const Levels& levels) {
    std::vector<LevelSummary> out;
    out.reserve(levels.size());
    for (const auto& entry : levels) {
        Quantity quantity = 0;
        for (const Order& order : entry.second) {
            quantity += order.remaining;
        }
        out.push_back(LevelSummary{entry.first, quantity});
    }
    return out;
}

}  // namespace

std::vector<LevelSummary> Orderbook::bidDepth() const {
    return summarize(bids_);
}

std::vector<LevelSummary> Orderbook::askDepth() const {
    return summarize(asks_);
}

void Orderbook::clear() {
    bids_.clear();
    asks_.clear();
    index_.clear();
}
