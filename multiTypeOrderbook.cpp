#include <iostream>
#include <map>
#include <set>
#include <cmath>
#include <ctime>
#include <deque>
#include <stack>
#include <limits>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <variant>
#include <optional>
#include <tuple>
#include <format>
#include <list>


enum class OrderType
{
    GoodTillCancel,
    FillandKill
};

enum class Side
{
    Buy,
    Sell
};

using Price = std::int32_t; //Price can be negative
using Quantity = std::uint32_t; //Quantity cannot be negative so use unsigned int
using OrderId = std::uint64_t; //OrderID cannot be negative so use unsigned int

//An order book can be thought of as two levels. Price and Quantity
//Struct LevelInfo will be used for some public API to get the state of the order book
struct LevelInfo
{
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

//Want to encapsulate levelInfo to represent our sides. Orderbook can have two sides. Each side is list of levels
class OrderbookLevelInfos
{
public:
    OrderbookLevelInfos(const LevelInfos& bids, const LevelInfos& asks)
    : bids_{ bids }
    , asks_{ asks }
    { }

    const LevelInfos& GetBids() const { return bids_ ; }
    const LevelInfos& GetAsks() const { return asks_; }

private:
    LevelInfos bids_;
    LevelInfos asks_;
};

//We have everything we need to represent internal state of order book
// Describe what we need to add to book. Order objects. Order objects contain type, ID, side, quantity, filled, etc

class Order
{
public:
    Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
    : orderType_{ orderType }
    , orderId_{ orderId }
    , side_{ side }
    , price_{ price }
    , initialQuantity_{ quantity } //Three different quantities: initial quantity of the order, quantity remaining, quantity filled. We need to keep track of two
    , remainingQuantity_{ quantity }
    { }

    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    OrderType GetOrderType() const { return orderType_; }
    Quantity GetInitialQuantity() const { return initialQuantity_; } //Might not even need this. Exchange documentation might not require it
    Quantity GetRemainingQuantity() const { return remainingQuantity_; }
    Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }
    bool isFilled() const { return GetRemainingQuantity() == 0; }
    //Now need to get APi to fill us
    //when a trade happens, lowest quantity associated between both orders is the quantity used to fill both orders
    void Fill(Quantity quantity)
    {
        if (quantity > GetRemainingQuantity())
            throw std::logic_error("Order cannot be filled: quantity exceeds remaining quantity");
        
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

//Want reference semantics to make things easier
using OrderPointer = std::shared_ptr<Order>;
//List gives us an iterator that cannot be invalidated despite the list growing very large. Useful to see where our order is in bids/ask orderbook
//Cost and tradeoffs. Not gonna be super high level, but gets the job done
using OrderPointers = std::list<OrderPointer>;

//Want to create an abstraction for an order that needs to be modified. Add, modified, cancel
class OrderModify
{
public:
    OrderModify(OrderId orderId, Side side, Price price, Quantity quantity)
        : orderId_{ orderId }
        , price_{ price }
        , side_{ side }
        , quantity_{ quantity }
    { }
    OrderId GetOrderId() const { return orderId_; }
    Price GetPrice() const { return price_; }
    Side GetSide() const { return side_; }
    Quantity GetQuantity() const { return quantity_; }
    //We will have one more public API that converts a given order that exists, transforming it into a new order
    OrderPointer ToOrderPointer(OrderType type) const
    {
        return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(), GetQuantity());
    }

private:
    OrderId orderId_;
    Price price_;
    Side side_;
    Quantity quantity_;
};

//When an order is matched. Use a trade object. Trade object is an aggregation of two trade info objects. One for bid and one for ask. 
struct TradeInfo
{
    OrderId orderId_;
    Price price_;
    Quantity quantity_;
};

class Trade
{
public:
    Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
    : bidTrade_{ bidTrade }
    , askTrade_{ askTrade }
    { }

    const TradeInfo& GetBidTrade() const { return bidTrade_; }
    const TradeInfo& GetAskTrade() const { return askTrade_; }

private: 
    TradeInfo bidTrade_;
    TradeInfo askTrade_;
};

//There can be more than one trade/more than one execution
using Trades = std::vector<Trade>;

class Orderbook
{
private:
    //When we store our orders, we think of maps and unordered maps
    //Map represents bids and asks. Bids are sorted in descending order. Asks are sorted in ascendign order
    //Specify an actual order
    //Also need easy O(1) access for an order based on its orderId
    struct OrderEntry
    {
        OrderPointer order_{ nullptr };
        OrderPointers::iterator location_;
    };

    std::map<Price, OrderPointers, std::greater<Price>> bids_;
    std::map<Price, OrderPointers, std::less<Price>> asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;

    //Non FAK, match the order to the order book
    bool CanMatch(Side side, Price price) const //Just need side and price. Const means not mutating anything
    {
        if (side == Side::Buy)
        {
            if (asks_.empty()) //if no asks, cannot match
                return false;

            const auto& [bestAsk, _] = *asks_.begin(); //Get best ask price
            return price >= bestAsk; //Can match if buy price is greater than or equal to best ask price
        }
        else
        {
            if (bids_.empty()) //if no bids, cannot match
                return false;
            const auto& [bestBid, _] = *bids_.begin(); //Get best bid price
            return price <= bestBid; //Can match if sell price is less than or equal to best bid price
        }
        
    }

    //Match function. Have orders in the order book that need to be resolved
    //Return trades that happened as a result of matching
    Trades MatchOrders()
    {
        Trades trades;
        trades.reserve(orders_.size());
        while(true)
        {
            if(bids_.empty() || asks_.empty()) //if no bids or asks we will break
                break;

            auto& [bidPrice, bids] = *bids_.begin();
            auto& [askPrice, asks] = *asks_.begin();

            if (bidPrice < askPrice) //No more matches possible
                break;

            while (bids.size() && asks.size())
            {
                auto& bid = bids.front(); //time price priority
                auto& ask = asks.front();

                Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());
                bid->Fill(quantity);
                ask->Fill(quantity);

                if (bid->isFilled())
                {
                    bids.pop_front(); //dont need in queue anymore
                    orders_.erase(bid->GetOrderId()); // dont need in orders either
                }

                if (ask->isFilled())
                {
                    asks.pop_front();
                    orders_.erase(ask->GetOrderId());
                }

                //What if we have no bids or asks remaining in this price level
                if (bids.empty())
                    bids_.erase(bidPrice);
                if (asks.empty())
                    asks_.erase(askPrice);
                //Execute a trade
                trades.push_back(Trade{ 
                    TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity },
                    TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity }
                });
            }
        }

        //If FOK order, and it hasn't been fully filled, its still gonna be in order book and we need to remove it
        if (!bids_.empty())
        {
            auto& [_, bids] = *bids_.begin();
            auto& order = bids.front();
            if (order->GetOrderType() == OrderType::FillandKill)
                CancelOrder(order->GetOrderId());
        }
        if (!asks_.empty())
        {
            auto& [_, asks] = *asks_.begin();
            auto& order = asks.front();
            if (order->GetOrderType() == OrderType::FillandKill)
                CancelOrder(order->GetOrderId());
        }

        return trades;
    }

public:

    //Everytime you add an oder you can match, return trades if any
    Trades AddOrder(OrderPointer order) //non const because you can mutate this
    { 
        if (orders_.find(order->GetOrderId()) != orders_.end())
            return { }; //Order already exists, cannot add again
        if (order->GetOrderType() == OrderType::FillandKill && !CanMatch(order->GetSide(), order->GetPrice()))
            return { }; //Cannot match FAK order, so we dont add it

        OrderPointers::iterator iterator;

        if (order->GetSide() == Side::Buy)
        { 
            auto& orders = bids_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1); //Get iterator to the last element we just added
        }
        else
        { 
            auto& orders = asks_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1); //Get iterator to the last element we just added
        }

        orders_.insert({ order->GetOrderId(), OrderEntry{ order, iterator } });
        return MatchOrders();
    }

    //Now we do cancel first. Modify is just a cancel and a replace. So need cancel

    void CancelOrder(OrderId orderId) 
    {
        if (orders_.find(orderId) == orders_.end())
            return; //Order does not exist, nothing to cancel

        const auto& [order, orderIterator] = orders_.at(orderId);
        
        if (order->GetSide() == Side::Sell)
        { 
            auto price = order->GetPrice();
            auto& orderPointers = asks_.at(price);
            orderPointers.erase(orderIterator); //Remove from order pointers list
            if (orderPointers.empty())
                asks_.erase(price); //If no more orders at this price level, remove the price level
        }
        else
        {
            auto price = order->GetPrice();
            auto& orders = bids_.at(price);
            orders.erase(orderIterator);
            if (orders.empty())
                bids_.erase(price);
        }

        orders_.erase(orderId); //Remove from orders map AFTER using the references
    }
    //Modify order
    Trades MatchOrder(OrderModify order)
    {
        if (orders_.find(order.GetOrderId()) == orders_.end())
            return { }; //Order does not exist, cannot modify

        const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
        CancelOrder(order.GetOrderId());
        return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
    }

    //Know how. many orders
    std::size_t Size() const { return orders_.size(); }

    //Get current state of order book
    OrderbookLevelInfos GetOrderInfos() const
    { 
        LevelInfos bidInfos, askInfos;
        bidInfos.reserve(bids_.size());
        askInfos.reserve(asks_.size());

        //apply same function to bids and asks
        auto CreateLevelInfos = [](Price price, const OrderPointers& orders)
        { 
            return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
            [](Quantity runningSum, const OrderPointer& order)
            { return runningSum + order -> GetRemainingQuantity(); } ) }; //Want remaining to see ho wmuch is aggregate left on this level
            };

        for (const auto& [price, orders] : bids_)
            bidInfos.push_back(CreateLevelInfos(price, orders));
        for (const auto& [price, orders] : asks_)
            askInfos.push_back(CreateLevelInfos(price, orders));

        return OrderbookLevelInfos{ bidInfos, askInfos };
    }
};

int main()
{
    Orderbook orderbook;
    const OrderId orderId = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
    std::cout << orderbook.Size() << std::endl;
    orderbook.CancelOrder(orderId);
    std::cout << orderbook.Size() << std::endl;
    return 0;
}
    
 