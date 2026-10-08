#pragma once

#include <cstdint>

using Price    = std::int32_t;
using Quantity = std::uint32_t;
using OrderId  = std::uint64_t;

enum class Side      { Buy, Sell };
enum class OrderType { GoodTillCancel, FillAndKill };
