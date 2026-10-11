#pragma once

#include "orderbook/order.hpp"

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

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

    std::map<Price, Level, std::greater<Price>> bids_; // syntax for map: <key, value, sort order> 
    std::map<Price, Level, std::less<Price>> asks_;
    std::unordered_map<OrderId, Level::iterator> index_; // Level::iterator is a bookmark into either asks or bids
}; // unordered map is a hashtable lol
