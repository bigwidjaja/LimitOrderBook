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
template <class Fn>
decltype(auto) withBook(Fn&& fn) {
    std::lock_guard<std::mutex> lock(bookMutex);
    return std::forward<Fn>(fn)(book);
}

void sendJson(httplib::Response& res, int status, const nlohmann::json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

std::optional<Side> parseSide(std::string_view text) {
    if (text == "BUY") {
        return Side::Buy;
    }
    if (text == "SELL") {
        return Side::Sell;
    }
    return std::nullopt;
}

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
    if (text.empty() || text.find('-') != std::string::npos) {
        return std::nullopt;
    }
    try {
        std::size_t used = 0;
        const unsigned long long value = std::stoull(text, &used);
        if (used != text.size() || value > static_cast<unsigned long long>(std::numeric_limits<T>::max())) {
            return std::nullopt;
        }
        return static_cast<T>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<Price> parsePrice(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t used = 0;
        const long long value = std::stoll(text, &used);
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
        return *price;
    }
    return nullptr;
}

nlohmann::json levelsJson(const std::vector<LevelSummary>& levels) {
    nlohmann::json array = nlohmann::json::array();
    for (const LevelSummary& level : levels) {
        array.push_back({{"price", level.price}, {"quantity", level.quantity}});
    }
    return array;
}

nlohmann::json statusJson(const Orderbook& current) {
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
    if (!id || !type || !side || !price || !quantity || *id == 0) {
        return std::nullopt;
    }
    return ParsedOrder{Order{*id, *side, *type, *price, *quantity}, book};
}

std::string jsonString(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    return {};
}

void registerRoutes(httplib::Server& server) {
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
        const auto parsed = parseFields(req.get_param_value("orderid"), req.get_param_value("tradetype"),
                                         req.get_param_value("side"), req.get_param_value("price"),
                                         req.get_param_value("quantity"), req.get_param_value("book"));
        if (!parsed) {
            sendJson(res, 400, {{"error", "Missing or invalid parameters"}});
            return;
        }
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
            body = nlohmann::json::parse(req.body);
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
                if (!item.is_object() || !item.contains("orderid") || item["orderid"].is_null()) {
                    continue;
                }
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
                BookTally& tally = tallies[parsed->book];
                tally.tradesExecuted += static_cast<int>(trades.size());
                for (const Trade& trade : trades) {
                    tally.volumeTraded += trade.quantity;
                }
            }

            nlohmann::json results = nlohmann::json::object();
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
