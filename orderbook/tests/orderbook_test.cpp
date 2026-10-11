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

TEST_CASE("full fill at the resting price") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Sell, OrderType::GoodTillCancel, 100, 5}).empty());

    auto trades = book.add({2, Side::Buy, OrderType::GoodTillCancel, 101, 5});

    REQUIRE(trades.size() == 1);
    CHECK(trades[0].buyId == 2);
    CHECK(trades[0].sellId == 1);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].quantity == 5);
    CHECK(book.size() == 0);
    CHECK_FALSE(book.bestBid().has_value());
    CHECK_FALSE(book.bestAsk().has_value());
}

TEST_CASE("partial fill, remainder rests") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Sell, OrderType::GoodTillCancel, 100, 5}).empty());

    auto trades = book.add({2, Side::Buy, OrderType::GoodTillCancel, 100, 12});

    REQUIRE(trades.size() == 1);
    CHECK(trades[0].buyId == 2);
    CHECK(trades[0].sellId == 1);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].quantity == 5);
    CHECK(book.size() == 1);
    CHECK(book.bestBid() == Price{100});
    CHECK_FALSE(book.bestAsk().has_value());

    auto cleanup = book.add({3, Side::Sell, OrderType::GoodTillCancel, 100, 7});
    REQUIRE(cleanup.size() == 1);
    CHECK(cleanup[0].quantity == 7);
    CHECK(book.size() == 0);
}

TEST_CASE("sweep two levels") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Sell, OrderType::GoodTillCancel, 100, 5}).empty());
    REQUIRE(book.add({2, Side::Sell, OrderType::GoodTillCancel, 101, 10}).empty());

    auto trades = book.add({3, Side::Buy, OrderType::GoodTillCancel, 101, 12});

    REQUIRE(trades.size() == 2);
    CHECK(trades[0].buyId == 3);
    CHECK(trades[0].sellId == 1);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].quantity == 5);
    CHECK(trades[1].buyId == 3);
    CHECK(trades[1].sellId == 2);
    CHECK(trades[1].price == 101);
    CHECK(trades[1].quantity == 7);
    CHECK(book.size() == 1);
    CHECK_FALSE(book.bestBid().has_value());
    CHECK(book.bestAsk() == Price{101});

    auto cleanup = book.add({4, Side::Buy, OrderType::GoodTillCancel, 101, 3});
    REQUIRE(cleanup.size() == 1);
    CHECK(cleanup[0].quantity == 3);
    CHECK(cleanup[0].sellId == 2);
    CHECK(book.size() == 0);
}

TEST_CASE("price-time priority") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Sell, OrderType::GoodTillCancel, 100, 10}).empty());
    REQUIRE(book.add({2, Side::Sell, OrderType::GoodTillCancel, 100, 10}).empty());

    auto trades = book.add({3, Side::Buy, OrderType::GoodTillCancel, 100, 10});

    REQUIRE(trades.size() == 1);
    CHECK(trades[0].buyId == 3);
    CHECK(trades[0].sellId == 1);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].quantity == 10);
    CHECK(book.size() == 1);
    CHECK(book.bestAsk() == Price{100});
    CHECK_FALSE(book.cancel(1));
    REQUIRE(book.cancel(2));
    CHECK(book.size() == 0);
}

TEST_CASE("no cross") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Sell, OrderType::GoodTillCancel, 105, 10}).empty());

    auto trades = book.add({2, Side::Buy, OrderType::GoodTillCancel, 100, 10});

    REQUIRE(trades.empty());
    CHECK(book.size() == 2);
    CHECK(book.bestBid() == Price{100});
    CHECK(book.bestAsk() == Price{105});
}

TEST_CASE("FillAndKill that cannot match is rejected") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Sell, OrderType::GoodTillCancel, 105, 10}).empty());

    auto trades = book.add({2, Side::Buy, OrderType::FillAndKill, 100, 10});

    REQUIRE(trades.empty());
    CHECK(book.size() == 1);
    CHECK(book.bestAsk() == Price{105});
    CHECK_FALSE(book.cancel(2));
}

TEST_CASE("FillAndKill trades and discards the remainder") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Sell, OrderType::GoodTillCancel, 100, 4}).empty());

    auto trades = book.add({2, Side::Buy, OrderType::FillAndKill, 100, 10});

    REQUIRE(trades.size() == 1);
    CHECK(trades[0].buyId == 2);
    CHECK(trades[0].sellId == 1);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].quantity == 4);
    CHECK(book.size() == 0);
    CHECK_FALSE(book.bestBid().has_value());
    CHECK_FALSE(book.cancel(2));
}

TEST_CASE("FillAndKill that fully fills leaves nothing behind") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Sell, OrderType::GoodTillCancel, 100, 10}).empty());

    auto trades = book.add({2, Side::Buy, OrderType::FillAndKill, 100, 10});

    REQUIRE(trades.size() == 1);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].quantity == 10);
    CHECK(book.size() == 0);
    CHECK_FALSE(book.bestBid().has_value());
    CHECK_FALSE(book.bestAsk().has_value());
}

TEST_CASE("a sell hits the best bid and trades at the bid") {
    Orderbook book;
    REQUIRE(book.add({1, Side::Buy, OrderType::GoodTillCancel, 100, 4}).empty());

    auto trades = book.add({2, Side::Sell, OrderType::GoodTillCancel, 99, 4});

    REQUIRE(trades.size() == 1);
    CHECK(trades[0].buyId == 1);
    CHECK(trades[0].sellId == 2);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].quantity == 4);
    CHECK(book.size() == 0);
    CHECK_FALSE(book.bestBid().has_value());
    CHECK_FALSE(book.bestAsk().has_value());
}
