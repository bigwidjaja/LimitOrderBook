#include "orderbook/orderbook.hpp"

#include <iterator>
#include <utility>

/*
 add
    Reject a duplicate id: return an empty vector and do not insert.
    Reject quantity 0 the same way.
    Otherwise append the order to the back of its price level
    (create the level if needed) and store the list iterator in index_.
    Return an empty trade vector.
*/

std::vector<Trade> Orderbook::add(Order order) {
    if (order.remaining == 0 || index_.contains(order.id)) { //.remaining is from the object order. index_. is a call to the map in the Orderbook.hpp
        return {};
    }

    if (order.side == Side::Buy) { // we make it enum Side::Buy cuz itd be bad if it were a string, it has to be one or the other
        Level& level = bids_[order.price]; // we create an item of type reference to a level which is our bids_ (a map) which have our current prices
        level.push_back(std::move(order)); // appends the order to the back
        index_[level.back().id] = std::prev(level.end()); //level.back() is the order we just added. were setting its index to the position of the last order
    } else { //we use std::prev cuz end goes to the very end, and we gotta step back by one. VISUAL: [ id 1 | id 2 | id 5 ] end() <- 

        Level& level = asks_[order.price];
        level.push_back(std::move(order));
        index_[level.back().id] = std::prev(level.end());
    }
    return {};
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
