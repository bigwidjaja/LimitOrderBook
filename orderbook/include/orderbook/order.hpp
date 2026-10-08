#pragma once

#include "orderbook/types.hpp"

struct Order {
    OrderId id;
    Side side;
    OrderType type;
    Price price;
    Quantity remaining;
};

struct Trade {
    OrderId buyId;
    OrderId sellId;
    Price price;
    Quantity quantity;
};
