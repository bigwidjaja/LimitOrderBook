#include "orderbook/orderbook.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

std::vector<Trade> Orderbook::add(Order order) {
    // Reject an order that wants to trade 0 units or if the id already exists.
    if (order.remaining == 0 || index_.contains(order.id)) {
        return {};
    }

    std::vector<Trade> trades;
    const bool isBuy = order.side == Side::Buy;

    // Match the best opposite price first, and the oldest order at that price. (price priority > time priority)
    auto matchAgainst = [&](auto& levels) { // the [&] is a lambda that lets us manipulate the variables IN the add function (uses auto since asks_ and bids_ can be diff types).
    //  extra note: auto on the left means it takes the type  from the right
        while (order.remaining > 0 && !levels.empty()) {
            const Price best = levels.begin()->first;
            if (isBuy ? order.price < best : order.price > best) { // if isBuy is true, use the left side of the colon
                break;
            }

            Level& level = levels.begin()->second; // levels refers to either asks_ or bids_ depending on which map we matched to
           // also, don't forget ->first is the price of the asks_ or bids_ map, while ->second is the list of orders at that price.
            Order& resting = level.front(); // resting is the order already in the book we trade against
            const Quantity quantity = std::min(order.remaining, resting.remaining);
            const Price tradePrice = resting.price;
            const OrderId restingId = resting.id;

            order.remaining -= quantity;
            resting.remaining -= quantity;
            // When incoming is buy, its id is the buyer and restingID is the seller. When incoming is a sell, restingId is the buyer and incoming id is the seller.
            if (isBuy) {
                trades.push_back(Trade{order.id, restingId, tradePrice, quantity});
            } else {
                trades.push_back(Trade{restingId, order.id, tradePrice, quantity});
            }

            // Destroy the node if we took the last units of resting
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

/* 
cancel
    Unknown id: return false.
    Known id: erase that list node, erase the index entry, erase the
    price level if its list is now empty, return true.
    Canceling the same id twice returns false the second time.
*/

bool Orderbook::cancel(OrderId id) {
    if (index_.contains(id) == false) {
        return false;
    }

    Level::iterator node = index_[id]; // node is the bookmark (our id)
    Price price = node->price; 
    Side side = node->side;  

    if (side == Side::Buy) { 
        Level& level = bids_[price]; // grab the list of bids_ at that price (our node's price) 
        level.erase(node); // removes the id at a specific level of bids_ :   100 -> [ id 1 | id 2 ]
        index_.erase(id); // removing our bookmark (our lookup entry) 
        if (level.empty()) { 
            bids_.erase(price); // if no more bids at that price, remove the entry from bids
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


/* 
 bestBid / bestAsk
    Empty side: std::nullopt. Otherwise the first key in that map.
*/

std::optional<Price> Orderbook::bestBid() const { 
    if (bids_.empty()) { 
        return std::nullopt;
    } else { 
        return bids_.begin()->first; 
    }
}

std::optional<Price> Orderbook::bestAsk() const {
    if (asks_.empty()) {
        return std::nullopt; 
    } else {
        return asks_.begin()->first; // asks_.begin returns the first entry which is 101 -> [sell order ...] so ->first returns that 101 (the first item)
    }
}

/* 
  this one self explanatory lol
*/

std::size_t Orderbook::size() const {
    return index_.size();
}

std::size_t Orderbook::bidCount() const {
    std::size_t count = 0;
    for (const auto& entry : bids_) { // for entries in bids
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
