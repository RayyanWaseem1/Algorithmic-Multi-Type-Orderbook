// Compile: g++ -std=c++17 OrderbookREST.cpp -o OrderbookREST -lcurl
//
//
// ./OrderbookREST


#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <curl/curl.h>

// Your orderbook types
using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

enum class Side {
    Buy,
    Sell
};

enum class OrderType {
    GoodTillCancel,  // Maps to Alpaca's "gtc"
    FillandKill      // Maps to Alpaca's "ioc" (Immediate or Cancel)
};

// Simplified Order class
class Order {
public:
    Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
        : orderType_(orderType)
        , orderId_(orderId)
        , side_(side)
        , price_(price)
        , initialQuantity_(quantity)
        , remainingQuantity_(quantity)
    { }

    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    OrderType GetOrderType() const { return orderType_; }
    Quantity GetInitialQuantity() const { return initialQuantity_; }
    Quantity GetRemainingQuantity() const { return remainingQuantity_; }
    Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }
    bool isFilled() const { return GetRemainingQuantity() == 0; }
    
    void Fill(Quantity quantity) {
        if (quantity > GetRemainingQuantity())
            throw std::logic_error("Cannot fill: quantity exceeds remaining");
        remainingQuantity_ -= quantity;
    }

private:
    OrderType orderType_;
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity initialQuantity_;
    Quantity remainingQuantity_;
};

using OrderPointer = std::shared_ptr<Order>;

// ==================== Simple JSON Parser ====================

class SimpleJsonParser {
public:
    static std::string extractString(const std::string& json, const std::string& key) {
        std::string searchKey = "\"" + key + "\":\"";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) {
            // Try without quotes around value (for numbers as strings)
            searchKey = "\"" + key + "\":";
            pos = json.find(searchKey);
            if (pos == std::string::npos) return "";
            pos += searchKey.length();
            
            // Skip whitespace
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
            
            // Check if it's a quoted string
            if (json[pos] == '\"') {
                pos++;
                size_t endPos = json.find("\"", pos);
                if (endPos == std::string::npos) return "";
                return json.substr(pos, endPos - pos);
            } else {
                // It's a number or boolean
                size_t endPos = json.find_first_of(",}]", pos);
                if (endPos == std::string::npos) return "";
                return json.substr(pos, endPos - pos);
            }
        }
        
        pos += searchKey.length();
        size_t endPos = json.find("\"", pos);
        if (endPos == std::string::npos) return "";
        
        return json.substr(pos, endPos - pos);
    }
    
    static double extractDouble(const std::string& json, const std::string& key) {
        std::string val = extractString(json, key);
        if (val.empty()) return 0.0;
        
        try {
            return std::stod(val);
        } catch (...) {
            return 0.0;
        }
    }
    
    static int extractInt(const std::string& json, const std::string& key) {
        std::string val = extractString(json, key);
        if (val.empty()) return 0;
        
        try {
            return std::stoi(val);
        } catch (...) {
            return 0;
        }
    }
    
    // Check if JSON contains an error
    static bool hasError(const std::string& json) {
        return json.find("\"code\":") != std::string::npos || 
               json.find("\"message\":") != std::string::npos;
    }
    
    // Extract error message
    static std::string extractError(const std::string& json) {
        std::string msg = extractString(json, "message");
        if (msg.empty()) msg = "Unknown error";
        return msg;
    }
};

// ==================== Alpaca REST API Client ====================

class AlpacaRestAPI {
private:
    std::string apiKey_;
    std::string apiSecret_;
    std::string baseUrl_;
    std::string dataUrl_;
    
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
    
    std::string MakeRequest(const std::string& endpoint, const std::string& params = "", 
                          const std::string& method = "GET", const std::string& body = "",
                          bool useDataAPI = false) {
        CURL* curl = curl_easy_init();
        std::string response;
        
        if (curl) {
            // Choose base URL
            std::string url = (useDataAPI ? dataUrl_ : baseUrl_) + endpoint;
            
            // Add query parameters
            if (!params.empty()) {
                url += "?" + params;
            }
            
            // Set up headers - Alpaca uses simple API key authentication!
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, ("APCA-API-KEY-ID: " + apiKey_).c_str());
            headers = curl_slist_append(headers, ("APCA-API-SECRET-KEY: " + apiSecret_).c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");
            
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            
            // Set HTTP method
            if (method == "POST") {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                if (!body.empty()) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                }
            } else if (method == "DELETE") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            } else if (method == "PATCH") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
                if (!body.empty()) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                }
            }
            
            CURLcode res = curl_easy_perform(curl);
            
            if (res != CURLE_OK) {
                std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
            }
            
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
        
        return response;
    }
    
public:
    AlpacaRestAPI(const std::string& apiKey = "", const std::string& apiSecret = "", bool usePaper = true) 
        : apiKey_(apiKey)
        , apiSecret_(apiSecret) {
        
        if (usePaper) {
            baseUrl_ = "https://paper-api.alpaca.markets";
            dataUrl_ = "https://data.alpaca.markets";
        } else {
            baseUrl_ = "https://api.alpaca.markets";
            dataUrl_ = "https://data.alpaca.markets";
        }
        
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    
    ~AlpacaRestAPI() {
        curl_global_cleanup();
    }
    
    // ==================== Account Information ====================
    
    // Get account information
    std::string GetAccount() {
        if (apiKey_.empty()) {
            return "{\"error\":\"API keys not configured\"}";
        }
        return MakeRequest("/v2/account");
    }
    
    // Get account buying power
    double GetBuyingPower() {
        std::string response = GetAccount();
        return SimpleJsonParser::extractDouble(response, "buying_power");
    }
    
    // Get account equity
    double GetEquity() {
        std::string response = GetAccount();
        return SimpleJsonParser::extractDouble(response, "equity");
    }
    
    // ==================== Market Data ====================
    
    // Get latest quote for a symbol
    std::string GetLatestQuote(const std::string& symbol) {
        return MakeRequest("/v2/stocks/" + symbol + "/quotes/latest", "", "GET", "", true);
    }
    
    // Get latest trade for a symbol
    std::string GetLatestTrade(const std::string& symbol) {
        return MakeRequest("/v2/stocks/" + symbol + "/trades/latest", "", "GET", "", true);
    }
    
    // Get snapshot (quote + trade + bars)
    std::string GetSnapshot(const std::string& symbol) {
        return MakeRequest("/v2/stocks/" + symbol + "/snapshot", "", "GET", "", true);
    }
    
    // Get bars (candles) for a symbol
    std::string GetBars(const std::string& symbol, const std::string& timeframe = "1Min", int limit = 100) {
        std::string params = "timeframe=" + timeframe + "&limit=" + std::to_string(limit);
        return MakeRequest("/v2/stocks/" + symbol + "/bars", params, "GET", "", true);
    }
    
    // ==================== Orders ====================
    
    // Get all orders
    std::string GetOrders(const std::string& status = "open") {
        if (apiKey_.empty()) {
            return "{\"error\":\"API keys not configured\"}";
        }
        
        std::string params = "status=" + status + "&limit=100";
        return MakeRequest("/v2/orders", params);
    }
    
    // Get specific order by ID
    std::string GetOrder(const std::string& orderId) {
        if (apiKey_.empty()) {
            return "{\"error\":\"API keys not configured\"}";
        }
        
        return MakeRequest("/v2/orders/" + orderId);
    }
    
    // Place a limit order
    std::string PlaceLimitOrder(const std::string& symbol, const std::string& side, 
                               int quantity, double limitPrice, const std::string& timeInForce = "gtc") {
        if (apiKey_.empty()) {
            return "{\"error\":\"API keys not configured\"}";
        }
        
        // Build JSON body
        std::stringstream body;
        body << "{"
             << "\"symbol\":\"" << symbol << "\","
             << "\"qty\":" << quantity << ","
             << "\"side\":\"" << side << "\","
             << "\"type\":\"limit\","
             << "\"time_in_force\":\"" << timeInForce << "\","
             << "\"limit_price\":" << std::fixed << std::setprecision(2) << limitPrice
             << "}";
        
        return MakeRequest("/v2/orders", "", "POST", body.str());
    }
    
    // Place a market order
    std::string PlaceMarketOrder(const std::string& symbol, const std::string& side, int quantity) {
        if (apiKey_.empty()) {
            return "{\"error\":\"API keys not configured\"}";
        }
        
        // Build JSON body
        std::stringstream body;
        body << "{"
             << "\"symbol\":\"" << symbol << "\","
             << "\"qty\":" << quantity << ","
             << "\"side\":\"" << side << "\","
             << "\"type\":\"market\","
             << "\"time_in_force\":\"day\""
             << "}";
        
        return MakeRequest("/v2/orders", "", "POST", body.str());
    }
    
    // Cancel order
    std::string CancelOrder(const std::string& orderId) {
        if (apiKey_.empty()) {
            return "{\"error\":\"API keys not configured\"}";
        }
        
        return MakeRequest("/v2/orders/" + orderId, "", "DELETE");
    }
    
    // Cancel all orders
    std::string CancelAllOrders() {
        if (apiKey_.empty()) {
            return "{\"error\":\"API keys not configured\"}";
        }
        
        return MakeRequest("/v2/orders", "", "DELETE");
    }
    
    // ==================== Positions ====================
    
    // Get all positions
    std::string GetPositions() {
        if (apiKey_.empty()) {
            return "{\"error\":\"API keys not configured\"}";
        }
        
        return MakeRequest("/v2/positions");
    }
    
    // Get position for specific symbol
    std::string GetPosition(const std::string& symbol) {
        if (apiKey_.empty()) {
            return "{\"error\":\"API keys not configured\"}";
        }
        
        return MakeRequest("/v2/positions/" + symbol);
    }
    
    // ==================== Utility ====================
    
    // Test connection
    bool TestConnection() {
        std::string response = GetAccount();
        return !response.empty() && response.find("\"id\":") != std::string::npos;
    }
    
    // Check if market is open
    std::string GetClock() {
        return MakeRequest("/v2/clock");
    }
    
    bool IsMarketOpen() {
        std::string response = GetClock();
        std::string isOpen = SimpleJsonParser::extractString(response, "is_open");
        return isOpen == "true";
    }
};

// ==================== Orderbook Manager ====================

class OrderbookManager {
private:
    AlpacaRestAPI& api_;
    std::string symbol_;
    std::vector<OrderPointer> localBids_;
    std::vector<OrderPointer> localAsks_;
    OrderId nextOrderId_;
    
public:
    OrderbookManager(AlpacaRestAPI& api, const std::string& symbol)
        : api_(api)
        , symbol_(symbol)
        , nextOrderId_(1)
    { }
    
    // Fetch and update local orderbook from exchange
    // Note: Alpaca doesn't provide full orderbook data like crypto exchanges
    // We simulate using bid/ask quotes
    bool UpdateFromExchange() {
        std::string response = api_.GetLatestQuote(symbol_);
        
        if (response.empty() || SimpleJsonParser::hasError(response)) {
            std::cerr << "Failed to fetch quote data" << std::endl;
            if (SimpleJsonParser::hasError(response)) {
                std::cerr << "Error: " << SimpleJsonParser::extractError(response) << std::endl;
            }
            return false;
        }
        
        // Clear local orderbook
        localBids_.clear();
        localAsks_.clear();
        
        // Extract bid/ask from quote
        // Alpaca returns: {"quote":{"ap":123.45,"as":100,"bp":123.40,"bs":50,...}}
        double askPrice = SimpleJsonParser::extractDouble(response, "ap");
        int askSize = SimpleJsonParser::extractInt(response, "as");
        double bidPrice = SimpleJsonParser::extractDouble(response, "bp");
        int bidSize = SimpleJsonParser::extractInt(response, "bs");
        
        if (bidPrice > 0 && bidSize > 0) {
            auto bidOrder = std::make_shared<Order>(
                OrderType::GoodTillCancel,
                nextOrderId_++,
                Side::Buy,
                static_cast<Price>(bidPrice * 100),  // Convert to cents
                static_cast<Quantity>(bidSize)
            );
            localBids_.push_back(bidOrder);
        }
        
        if (askPrice > 0 && askSize > 0) {
            auto askOrder = std::make_shared<Order>(
                OrderType::GoodTillCancel,
                nextOrderId_++,
                Side::Sell,
                static_cast<Price>(askPrice * 100),
                static_cast<Quantity>(askSize)
            );
            localAsks_.push_back(askOrder);
        }
        
        return true;
    }
    
    // Display orderbook
    void PrintOrderbook(int levels = 5) const {
        std::cout << "\n╔══════════════════════════════════╗" << std::endl;
        std::cout << "║  " << std::left << std::setw(27) << symbol_ << "║" << std::endl;
        std::cout << "╠══════════════════════════════════╣" << std::endl;
        
        // Print asks (highest to lowest)
        int askCount = std::min(levels, (int)localAsks_.size());
        for (int i = askCount - 1; i >= 0; i--) {
            double price = localAsks_[i]->GetPrice() / 100.0;
            int qty = localAsks_[i]->GetRemainingQuantity();
            printf("║ ASK  $%-8.2f  x  %-6d   ║\n", price, qty);
        }
        
        // Calculate spread
        if (!localBids_.empty() && !localAsks_.empty()) {
            double bidPrice = localBids_[0]->GetPrice() / 100.0;
            double askPrice = localAsks_[0]->GetPrice() / 100.0;
            double spread = askPrice - bidPrice;
            double spreadPercent = (spread / askPrice) * 100.0;
            printf("║ ─ SPREAD: $%.2f (%.2f%%) ─   ║\n", spread, spreadPercent);
        }
        
        // Print bids (highest to lowest)
        int bidCount = std::min(levels, (int)localBids_.size());
        for (int i = 0; i < bidCount; i++) {
            double price = localBids_[i]->GetPrice() / 100.0;
            int qty = localBids_[i]->GetRemainingQuantity();
            printf("║ BID  $%-8.2f  x  %-6d   ║\n", price, qty);
        }
        
        std::cout << "╚══════════════════════════════════╝\n" << std::endl;
    }
    
    double GetBestBid() const {
        return localBids_.empty() ? 0.0 : localBids_[0]->GetPrice() / 100.0;
    }
    
    double GetBestAsk() const {
        return localAsks_.empty() ? 0.0 : localAsks_[0]->GetPrice() / 100.0;
    }
    
    double GetMidPrice() const {
        if (localBids_.empty() || localAsks_.empty()) return 0.0;
        return (GetBestBid() + GetBestAsk()) / 2.0;
    }
    
    double GetSpread() const {
        if (localBids_.empty() || localAsks_.empty()) return 0.0;
        return GetBestAsk() - GetBestBid();
    }
};

// ==================== Simple Trading Strategy ====================

class SimpleSpreadStrategy {
private:
    AlpacaRestAPI& api_;
    OrderbookManager& orderbookMgr_;
    std::string symbol_;
    double targetSpreadPercent_;
    
public:
    SimpleSpreadStrategy(AlpacaRestAPI& api, OrderbookManager& orderbookMgr, 
                        const std::string& symbol, double targetSpreadPercent = 0.05)
        : api_(api)
        , orderbookMgr_(orderbookMgr)
        , symbol_(symbol)
        , targetSpreadPercent_(targetSpreadPercent)
    { }
    
    void Analyze() {
        double mid = orderbookMgr_.GetMidPrice();
        double spread = orderbookMgr_.GetSpread();
        
        if (mid == 0.0) {
            std::cout << "📊 No valid market data available" << std::endl;
            return;
        }
        
        double spreadPercent = (spread / mid) * 100.0;
        
        std::cout << "📊 Strategy Analysis:" << std::endl;
        std::cout << "  Mid Price: $" << std::fixed << std::setprecision(2) << mid << std::endl;
        std::cout << "  Spread: $" << spread << " (" << spreadPercent << "%)" << std::endl;
        
        if (spreadPercent > targetSpreadPercent_) {
            std::cout << "  💡 Opportunity: Spread is wide!" << std::endl;
            std::cout << "     Could place orders at:" << std::endl;
            std::cout << "     Buy:  $" << (mid - spread/4) << std::endl;
            std::cout << "     Sell: $" << (mid + spread/4) << std::endl;
        } else {
            std::cout << "  ⏸️  Spread too narrow, waiting..." << std::endl;
        }
        std::cout << std::endl;
    }
};

// ==================== Main Program ====================

int main() {
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Alpaca REST API + Orderbook           ║" << std::endl;
    std::cout << "║  Trading System (Paper Trading)        ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝\n" << std::endl;
    
    std::string apiKey = "API KEY";     /////// Your Alpaca API Key //////
    std::string apiSecret = "SECRET KEY";  /////// Your Alpaca Secret Key//////
    
    const char* envKey = std::getenv("ALPACA_API_KEY");
    const char* envSecret = std::getenv("ALPACA_SECRET_KEY");
    
    if (envKey && envSecret) {
        apiKey = envKey;
        apiSecret = envSecret;
        std::cout << "✅ Using API keys from environment variables\n" << std::endl;
    } else if (apiKey.empty() || apiSecret.empty()) {
        std::cout << "⚠️  No API keys configured!" << std::endl;
        std::cout << "Get free paper trading keys from: https://alpaca.markets/\n" << std::endl;
        std::cout << "Set environment variables:" << std::endl;
        std::cout << "  export ALPACA_API_KEY= " << std::endl;
        std::cout << "  export ALPACA_SECRET_KEY=  "<< std::endl;
        return 1;
    }
    
    AlpacaRestAPI api(apiKey, apiSecret, true);  // true = paper trading
    
    // Test connection
    std::cout << "Testing connection..." << std::endl;
    if (api.TestConnection()) {
        std::cout << "✅ Connected to Alpaca!\n" << std::endl;
    } else {
        std::cerr << "❌ Connection failed!" << std::endl;
        std::cerr << "Check your API keys and internet connection." << std::endl;
        return 1;
    }
    
    // Get account info
    std::string accountInfo = api.GetAccount();
    double buyingPower = SimpleJsonParser::extractDouble(accountInfo, "buying_power");
    double equity = SimpleJsonParser::extractDouble(accountInfo, "equity");
    
    std::cout << "💰 Account Info:" << std::endl;
    std::cout << "  Equity: $" << std::fixed << std::setprecision(2) << equity << std::endl;
    std::cout << "  Buying Power: $" << buyingPower << "\n" << std::endl;
    
    // Check if market is open
    std::string clockInfo = api.GetClock();
    bool isOpen = api.IsMarketOpen();
    std::cout << "🕐 Market Status: " << (isOpen ? "OPEN ✅" : "CLOSED ⏸️") << "\n" << std::endl;
    
    // Choose a stock symbol (use liquid stocks for better quotes)
    std::string symbol = "AAPL";  // Apple Inc.
    // Other options: "SPY", "TSLA", "MSFT", "GOOGL", "AMZN"
    
    // Get latest trade price
    std::cout << "Fetching " << symbol << " latest trade..." << std::endl;
    std::string tradeResponse = api.GetLatestTrade(symbol);
    double lastPrice = SimpleJsonParser::extractDouble(tradeResponse, "p");
    std::cout << "💰 " << symbol << " Last Price: $" << lastPrice << "\n" << std::endl;
    
    // Initialize orderbook manager
    OrderbookManager orderbookMgr(api, symbol);
    
    // Initialize strategy
    SimpleSpreadStrategy strategy(api, orderbookMgr, symbol, 0.02);
    
    // Main loop - update orderbook every 2 seconds
    std::cout << "Starting trading system...\n" << std::endl;
    std::cout << "Press Ctrl+C to exit\n" << std::endl;
    
    for (int i = 0; i < 10; i++) {  // Run for 20 seconds
        std::cout << "═══ Update " << (i + 1) << " ═══" << std::endl;
        
        // Fetch and update orderbook
        if (orderbookMgr.UpdateFromExchange()) {
            // Display orderbook
            orderbookMgr.PrintOrderbook(5);
            
            // Run strategy
            strategy.Analyze();
        }
        
        // Wait 2 seconds
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    std::cout << "\n✅ Trading session complete!" << std::endl;
    std::cout << "\n💡 Next Steps:" << std::endl;
    std::cout << "  1. This is paper trading - experiment freely!" << std::endl;
    std::cout << "  2. Implement your trading strategy" << std::endl;
    std::cout << "  3. Add risk management" << std::endl;
    std::cout << "  4. Test order placement with api.PlaceLimitOrder()" << std::endl;
    std::cout << "  5. Paper trade for 1+ month before considering live trading\n" << std::endl;
    
    return 0;
}