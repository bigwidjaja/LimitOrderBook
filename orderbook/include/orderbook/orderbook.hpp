#pragma once

#include "orderbook/order.hpp"

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

// Quantity at one price. The list behind it is private to Orderbook.
struct LevelSummary {
    Price price;
    Quantity quantity;
};

class Orderbook {
public:
    [[nodiscard]] std::vector<Trade> add(Order order);
    bool cancel(OrderId id);
    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    std::size_t size() const;
    std::size_t bidCount() const;
    std::size_t askCount() const;
    std::size_t bidLevels() const;
    std::size_t askLevels() const;
    [[nodiscard]] std::vector<LevelSummary> bidDepth() const;
    [[nodiscard]] std::vector<LevelSummary> askDepth() const;
    void clear();

private:
    using Level = std::list<Order>;

    // begin() is the best price: highest bid, lowest ask.
    std::map<Price, Level, std::greater<Price>> bids_;
    std::map<Price, Level, std::less<Price>> asks_;
    // Points at the list node that owns the order. Cancel uses it instead of scanning.
    std::unordered_map<OrderId, Level::iterator> index_;
};
