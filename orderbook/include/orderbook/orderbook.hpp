#pragma once

#include "orderbook/order.hpp"

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

class Orderbook {
public:
    [[nodiscard]] std::vector<Trade> add(Order order);
    bool cancel(OrderId id);
    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    std::size_t size() const;

private:
    using Level = std::list<Order>;

    std::map<Price, Level, std::greater<Price>> bids_;
    std::map<Price, Level, std::less<Price>> asks_;
    std::unordered_map<OrderId, Level::iterator> index_;
};
