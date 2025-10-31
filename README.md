# Multi Type Orderbook and REST API Trading Engine
## Overview and Motivation
The motivation for this project stemmed from the desire to bridge the gap between market theory and real-world trading system implementation. While most introductory orderbook models traditionally only consider simple limit-order functionality, most actual exchanges operate under a wide variety of order types, execution conditions, and time-in-force constraints. 

This project introduces a multi-type orderbook that supports both Good-Till-Cancel (GTC) and Fill-And-Kill (FAK) orders. It models how exchanges prioritize, match, and execute trades while also managing liquidty at multiple price levels. The goal was to create a modular engine that not only simulates the mechanics of real-time matching but could also integrate directly with live market data and REST APIs for paper trading (potentially live trading), execution testing, and further research on algorithmic trading strategies

By combining a C++ matching engine with a REST-based Alpaca API extension, this project allows future users to simulate realistic market conditions, monitor live orderbooks, and even execute trades - all within a clean architecture.

## Methodology and System Architecture
The system is divided into two main components: the orderbook engine, implemented in multiTypeOrderbook.cpp, and the REST-integrated trading module, implemented in OrderbookREST.cpp.

The orderbook enginge models a marketplace composed of bids and asks stored in efficient map-based structures, each keyed by price. The bid side is sorted in descending order to prioritize higher bids, while the ask side is sorted ascendingly to prioritize lower offers. This is similar in nature to a traditional orderbook. Each price level holds a list of shared pointers to the "Order" object, which contains essential features such as price, quantity, side, and order type. The use of shared pointers allows stable memory references even as the orderbook grows. 

Matching occurs under a price-time priority regime: orders are executed when a bid price meets or exceeds the best ask price. Quantities are filled based on the smaller remaining order, and filled orders are removed automatically. The logic ensures that FAK ordres that are not immediately matched are discardded, maintaining realistic execution constraints. Each match produces a "Trade" object which records execution details such as price, quanitity, and order IDs. 

The second module, OrderbookREST.cpp, connects this infrastructure directly to the Alpaca Markets REST API. Through the lightweight C++ wrappers built on libcurl, it allows authenticated requests for account information, market quotes, trade snapshots, and order placements. The inclusion of the JSON parser enables the system to parse responses without reliance on third-party libraries. 

The OrderbookManager class synchronizes the local orderbook with Alpaca's market data, fetching bid-ask information for symbols such as AAPL or SPY. This data is then displayed in the terminal view showing live prices, quantities, and spreads. 

## API Usage and Trade Execution
The integrated REST client enables both market observation as well as trade execution. It supports account queries, order submissions, and position management using Alpaca's paper trading environment, however live trading is also available.

After initializing the connection with valid API keys, the engine can query account balances, retrieve live quotes, and place market or limit orders. 

## Real-World Applications and Experimental Results
It has several real-world use cases. In algorithmic trading research, it serves as a foundation for testing limit-order placement, spread-capture, and mean-reversion strategies. 

Beyond experimentation, this framework can also potentially function as a backtesting engine, enabling retail traders to replay historical data and benchmark strategy performance under different market regimes. 

## Conclusion
This Multi-Type Orderbook and REST Trading Engine demonstrates how a high-performance C++ architecture can capture the core logic of modern exchanges while interacting seamlessly with trading APIs. Designed with extensibility and academic rigor in mind, this project can evolve into a full-fledged trading system, or sandbox for developing machine learning driven execution algorithms. 
