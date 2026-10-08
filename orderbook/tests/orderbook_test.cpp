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

TEST_CASE("cancel removes a resting order") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Buy, OrderType::GoodTillCancel, 99, 10}).empty());
    REQUIRE(book.cancel(1));
    CHECK_FALSE(book.cancel(1));
    CHECK_FALSE(book.bestBid().has_value());
    CHECK(book.size() == 0);
}

TEST_CASE("cancel of an unknown id does nothing") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Buy, OrderType::GoodTillCancel, 100, 10}).empty());
    CHECK_FALSE(book.cancel(2));
    CHECK(book.size() == 1);
    CHECK(book.bestBid() == Price{100});
}

TEST_CASE("the last order at a price removes that level") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Buy, OrderType::GoodTillCancel, 100, 10}).empty());
    REQUIRE(book.add({2, Side::Buy, OrderType::GoodTillCancel, 100, 5}).empty());
    REQUIRE(book.cancel(1));
    CHECK(book.bestBid() == Price{100});
    CHECK(book.size() == 1);
    REQUIRE(book.cancel(2));
    CHECK_FALSE(book.bestBid().has_value());
    CHECK(book.size() == 0);
}

TEST_CASE("duplicate ids are rejected") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Buy, OrderType::GoodTillCancel, 100, 10}).empty());
    auto trades = book.add({1, Side::Sell, OrderType::GoodTillCancel, 101, 4});
    REQUIRE(trades.empty());
    CHECK(book.size() == 1);
    CHECK(book.bestBid() == Price{100});
    CHECK_FALSE(book.bestAsk().has_value());
}

TEST_CASE("a zero-quantity order is rejected") {
    Orderbook book;
    auto trades = book.add({1, Side::Buy, OrderType::GoodTillCancel, 100, 0});
    REQUIRE(trades.empty());
    CHECK(book.size() == 0);
    CHECK_FALSE(book.bestBid().has_value());
}

TEST_CASE("best bid and best ask are empty on a new book") {
    Orderbook book;
    CHECK_FALSE(book.bestBid().has_value());
    CHECK_FALSE(book.bestAsk().has_value());
}
