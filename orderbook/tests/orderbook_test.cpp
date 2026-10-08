#include "orderbook/orderbook.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("an order that does not cross rests") {
    Orderbook book;
    auto trades = book.add(
        {1, Side::Buy, OrderType::GoodTillCancel, 100, 10});
    REQUIRE(trades.empty());
    CHECK(book.size() == 1);
    CHECK(book.bestBid() == Price{100});
    CHECK_FALSE(book.bestAsk().has_value());
}
