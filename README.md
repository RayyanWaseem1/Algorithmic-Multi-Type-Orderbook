# Multi Type Orderbook and REST API Trading Engine
## Overview and Motivation
The motivation for this project stemmed from the desire to bridge the gap between market theory and real-world trading system implementation. While most introductory orderbook models traditionally only consider simple limit-order functionality, most actual exchanges operate under a wide variety of order types, execution conditions, and time-in-force constraints. 

This project introduces a multi-type orderbook that supports both Good-Till-Cancel (GTC) and Fill-And-Kill (FAK) orders. It models how exchanges prioritize, match, and execute trades while also managing liquidty at multiple price levels. The goal was to create a modular engine that not only simulates the mechanics of real-time matching but could also integrate directly with live market data and REST APIs for paper trading (potentially live trading), execution testing, and further research on algorithmic trading strategies

By combining a C++ matching engine with a REST-based Alpaca API extension, this project allows future users to simulate realistic market conditions, monitor live orderbooks, and even execute trades - all within a clean architecture.
