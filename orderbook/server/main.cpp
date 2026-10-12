#include "orderbook/orderbook.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

/* Small reminders
std::cout normal output
std::cerr is the error stream
std::flush pushes text immediately
std::stoi turns text to num
*/

namespace { // When a namespace has no name, anything inside it can be used in this .cpp file and nowhere else. portFromArgs is a helper for this file only. Other files cannot call it.
// Remember, std::cout means cout from stdlib (std is the namespace)

// We pick a port, and wait for the HTTP request
int portFromArgs(int argc, char* argv[]) { // argc is how many pieces were typed while argv is the pieces themselves
    constexpr int defaultPort = 6060;
    if (argc <= 1) {
        return defaultPort;
    }

    try {
        const int port = std::stoi(argv[1]);
        if (port < 1024 || port > 65535) {
            std::cerr << "Port must be between 1024 and 65535, using default 6060\n";
            return defaultPort;
        }
        return port;
    } catch (const std::exception&) {
        std::cerr << "Invalid port argument, using default 6060\n";
        return defaultPort;
    }
}

Orderbook book;
std::mutex bookMutex; // Lets one request in (such as add) done on a book, makes others wait

// Routes call this so only one request can touch the book at a time.
template <class Fn> // Fn is a placeholder for the type of function passed in, like /status, /cancel, /trade
decltype(auto) withBook(Fn&& fn) { // decltype(auto) means the func returns the exact type of its return expression. so if the inner returns a bool, itll return a bool etc
    std::lock_guard<std::mutex> lock(bookMutex); // Uses && meaning "take this argument as it was passed, without forcing an extra copy"
    return std::forward<Fn>(fn)(book); // std::forward<fn> means forward that kind of function.
}

// res is the HTTP reply httplib will send. & means we change that reply, not a copy.
// body is the JSON object. const & means we only read it.
void sendJson(httplib::Response& res, int status, const nlohmann::json& body) {
    res.status = status; // 200, 400, 404, etc
    // dump() turns the JSON object into text. "application/json" tells the caller that text is JSON.
    res.set_content(body.dump(), "application/json");
}

// Returns a Side or an empty optional if the text is neither BUY nor SELL.
// string_view looks at existing text, does not copy the string/
std::optional<Side> parseSide(std::string_view text) { // Optional means it returns a <Side> or nothing
    if (text == "BUY") {
        return Side::Buy;
    }
    if (text == "SELL") {
        return Side::Sell;
    }
    return std::nullopt; // no Side
}

// Same shape as parseSide
std::optional<OrderType> parseType(std::string_view text) {
    if (text == "GTC") {
        return OrderType::GoodTillCancel;
    }
    if (text == "FAK") {
        return OrderType::FillAndKill;
    }
    return std::nullopt; 
}

template <class T>
std::optional<T> parseInteger(const std::string& text) {
    // npos means "not found". find returns npos when '-' is not in the text.
    // A minus sign is rejected because this parses unsigned counts and ids.
    if (text.empty() || text.find('-') != std::string::npos) {
        return std::nullopt; // empty optional: not a valid integer
    }
    try {
        std::size_t used = 0;
        // unsigned long long is the largest unsigned integer type (at least 64 bits).
        // stoull means string to unsigned long long. &used is how many characters it consumed.
        const unsigned long long value = std::stoull(text, &used);
        // static_cast<NewType>(value) converts value to NewType. Use it when you already
        // checked the conversion is safe, instead of a C-style (NewType)value cast.
        if (used != text.size() || value > static_cast<unsigned long long>(std::numeric_limits<T>::max())) {
            return std::nullopt;
        }
        return static_cast<T>(value); // narrow the big integer to T, such as OrderId or Quantity
    } catch (const std::exception&) {
        return std::nullopt; // "12abc" or a number too huge for unsigned long long
    }
}

/* Quick lesson on signed/unsigned and bits 

Use a signed type when the value can go below zero or can be a difference. Prices, temperatures, account balances, and “how much did this change?” can be negative. A 32-bit signed integer holds about ±2 billion. That is enough for a price in ticks. It is not enough for a count of nanoseconds since 1970.

Use an unsigned type when a negative value is nonsense. Unsigned spends every bit on positive numbers, so a 32-bit unsigned value goes to about 4 billion instead of 2 billion. The trap is subtraction: if you take 10 away from 5, it does not become −5. It wraps to about 4 billion. That is why “how many are left?” has to be checked before you subtract.

Use a fixed-size type when the number leaves your program. A file, a network message, or a database column has a defined width. int is 32 bits on your Mac and also 32 bits on Windows, but long is 64 bits on your Mac and 32 bits on Windows. If one side writes 8 bytes and the other reads 4, every field after that is garbage. std::int32_t and std::uint64_t are the same size everywhere.

Use 64 bits when 32 bits is too small. A 32-bit id runs out at 4 billion. A user id, an order id, or a millisecond timestamp will get there. A 64-bit count is enough for those. It costs twice the memory of a 32-bit count, which matters when you store millions of them, and does not matter for one port number.

Use std::size_t for “how many things are in this container.” vector.size() and a loop index over a container are this type. It is unsigned and wide enough to name any position in memory. On your Mac that is 64 bits.

Use plain int for a small local number that never gets stored or sent. A loop that runs 10 times, or a port, does not need a fixed width. Do not use it for money, ids, or anything written to a file.

Parse text into the widest type, then shrink it. "12" arrives as characters. std::stoull turns it into an unsigned long long, the largest unsigned integer. Then you check it fits in an order id or a quantity before converting. If you convert first and check later, a too-big number has already wrapped.

*/

std::optional<Price> parsePrice(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t used = 0;
        // stoll is string to signed long long, so a price can be negative. stoull cannot.
        const long long value = std::stoll(text, &used);
        // ::min() is the smallest number Price can hold. ::max() is the largest.
        if (used != text.size() || value < std::numeric_limits<Price>::min() ||
            value > std::numeric_limits<Price>::max()) {
            return std::nullopt;
        }
        return static_cast<Price>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

nlohmann::json priceOrNull(std::optional<Price> price) {
    if (price.has_value()) {
        return *price; // * takes the Price out of the optional
    }
    return nullptr; // nlohmann turns nullptr into JSON null
}

nlohmann::json levelsJson(const std::vector<LevelSummary>& levels) {
    nlohmann::json array = nlohmann::json::array(); // an empty JSON list, []
    for (const LevelSummary& level : levels) {
        // {{"price", n}, {"quantity", n}} is one JSON object. push_back appends it.
        array.push_back({{"price", level.price}, {"quantity", level.quantity}});
    }
    return array;
}

nlohmann::json statusJson(const Orderbook& current) {
    // { {"name", value}, ... } builds one JSON object.
    return {
        {"bestBid", priceOrNull(current.bestBid())},
        {"bestAsk", priceOrNull(current.bestAsk())},
        {"size", current.size()},
        {"bidCount", current.bidCount()},
        {"askCount", current.askCount()},
        {"bidLevels", current.bidLevels()},
        {"askLevels", current.askLevels()},
        {"bids", levelsJson(current.bidDepth())},
        {"asks", levelsJson(current.askDepth())},
    };
}

nlohmann::json bookResult(const Orderbook& current, int tradesExecuted, std::uint64_t volumeTraded) {
    return {
        {"tradesExecuted", tradesExecuted},
        {"volumeTraded", volumeTraded},
        {"remainingBids", current.bidCount()},
        {"remainingAsks", current.askCount()},
        {"bestBidPrice", priceOrNull(current.bestBid())},
        {"bestAskPrice", priceOrNull(current.bestAsk())},
        {"bidLevels", current.bidLevels()},
        {"askLevels", current.askLevels()},
    };
}

struct ParsedOrder { 
    Order order;
    std::string book;
};

std::optional<ParsedOrder> parseFields(const std::string& idText, const std::string& typeText,
                                       const std::string& sideText, const std::string& priceText,
                                       const std::string& quantityText, const std::string& book) {
    if (idText.empty() || typeText.empty() || sideText.empty() || priceText.empty() || quantityText.empty() ||
        book.empty()) {
        return std::nullopt;
    }
    const auto id = parseInteger<OrderId>(idText);
    const auto type = parseType(typeText);
    const auto side = parseSide(sideText);
    const auto price = parsePrice(priceText);
    const auto quantity = parseInteger<Quantity>(quantityText);
    // An optional is false when it is empty. !id means parsing failed.
    // *id is the number inside, and only after that check.
    if (!id || !type || !side || !price || !quantity || *id == 0) {
        return std::nullopt;
    }
    // Order{...} fills the struct fields in order. ParsedOrder holds that plus the symbol.
    return ParsedOrder{Order{*id, *side, *type, *price, *quantity}, book};
}

std::string jsonString(const nlohmann::json& value) {
    // get<std::string>() pulls the JSON value out as that C++ type.
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    return {}; // empty string when the JSON value is neither text nor a number
}

void registerRoutes(httplib::Server& server) {
    // server.Get(path, function) tells httplib to call that function for this path.
    // [] captures nothing. [&] can use the local variables in this route, such as parsed.
    // The request parameter has no name because /status does not read the request.
    server.Get("/status", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, withBook([](const Orderbook& current) { return statusJson(current); }));
        std::cout << "GET /status\n";
    });

    server.Post("/reset", [](const httplib::Request&, httplib::Response& res) {
        const int cleared = withBook([](Orderbook& current) {
            const int booksCleared = current.size() == 0 ? 0 : 1;
            current.clear();
            return booksCleared;
        });
        sendJson(res, 200, {{"message", "All orderbooks cleared"}, {"booksCleared", cleared}});
        std::cout << "POST /reset\n";
    });

    server.Post("/trade", [](const httplib::Request& req, httplib::Response& res) {
        // get_param_value reads one form field from the body, such as orderid=1&side=BUY.
        const auto parsed = parseFields(req.get_param_value("orderid"), req.get_param_value("tradetype"),
                                         req.get_param_value("side"), req.get_param_value("price"),
                                         req.get_param_value("quantity"), req.get_param_value("book"));
        if (!parsed) {
            sendJson(res, 400, {{"error", "Missing or invalid parameters"}});
            return;
        }
        // parsed is an optional. parsed->order is the Order inside it.
        // (void) ignores the returned trades. add is nodiscard, so the compiler requires that.
        withBook([&](Orderbook& current) { (void)current.add(parsed->order); });
        sendJson(res, 200, {{"message", "Order placed successfully"}});
        std::cout << "POST /trade\n";
    });

    server.Post("/cancel", [](const httplib::Request& req, httplib::Response& res) {
        const auto id = parseInteger<OrderId>(req.get_param_value("orderid"));
        if (!id || req.get_param_value("book").empty()) {
            sendJson(res, 400, {{"error", "Missing required parameters"}});
            return;
        }
        const bool removed = withBook([&](Orderbook& current) { return current.cancel(*id); });
        if (removed) {
            sendJson(res, 200, {{"message", "Order Info Received"}});
        } else {
            sendJson(res, 404, {{"message", "Order ID not found"}});
        }
        std::cout << "POST /cancel\n";
    });

    server.Post("/batch", [](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body); // req.body is the raw POST text. parse turns it into JSON.
        } catch (const nlohmann::json::exception&) {
            sendJson(res, 400, {{"error", "No orders provided in batch"}});
            return;
        }
        if (!body.contains("orders") || !body["orders"].is_array() || body["orders"].empty()) {
            sendJson(res, 400, {{"error", "No orders provided in batch"}});
            return;
        }

        struct BookTally {
            int tradesExecuted = 0;
            std::uint64_t volumeTraded = 0;
        };
        int processedCount = 0;
        std::map<std::string, BookTally> tallies;

        withBook([&](Orderbook& current) {
            for (const auto& item : body["orders"]) {
                // contains asks if the key exists. is_null is JSON null. continue skips to the next order.
                if (!item.is_object() || !item.contains("orderid") || item["orderid"].is_null()) {
                    continue;
                }
                // item["orderid"] is that field. value("side", empty) uses the empty JSON if "side" is missing.
                const auto parsed = parseFields(jsonString(item["orderid"]), jsonString(item.value("tradetype", nlohmann::json{})),
                                                 jsonString(item.value("side", nlohmann::json{})),
                                                 jsonString(item.value("price", nlohmann::json{})),
                                                 jsonString(item.value("quantity", nlohmann::json{})),
                                                 jsonString(item.value("book", nlohmann::json{})));
                if (!parsed) {
                    continue;
                }
                const auto trades = current.add(parsed->order);
                ++processedCount;
                // map[key] finds that symbol, or creates a zero tally if this is its first order.
                BookTally& tally = tallies[parsed->book];
                tally.tradesExecuted += static_cast<int>(trades.size());
                for (const Trade& trade : trades) {
                    tally.volumeTraded += trade.quantity;
                }
            }

            nlohmann::json results = nlohmann::json::object();
            // [symbol, tally] splits each map pair into the key and the value.
            for (const auto& [symbol, tally] : tallies) {
                results[symbol] = bookResult(current, tally.tradesExecuted, tally.volumeTraded);
            }
            sendJson(res, 200, {{"processedCount", processedCount}, {"results", results}});
        });
        std::cout << "POST /batch\n";
    });
}

}  // namespace

int main(int argc, char* argv[]) {
    const int port = portFromArgs(argc, argv);

    httplib::Server server;
    registerRoutes(server);
    std::cout << "C++ server listening on http://localhost:" << port << '\n' << std::flush;
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Failed to listen on port " << port << '\n';
        return 1;
    }
    return 0;
}
