#pragma once

#include "EventQueue.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

class SimulatedEngine {
private:
    std::unordered_map<uint32_t, MarketDataPayload> marketCache;
    std::unordered_map<uint32_t, std::vector<OrderPayload>> pendingOrders;

    uint64_t fillCounter{0};
    double commissionPerShare{0.0005};
    double slippageBps{1.0}; 

    void generateFill(const OrderPayload& order, double fillPrice, double fillQty, EventQueue& queue, int64_t timestamp) {
        double rawPrice = fillPrice;
        double slippageDelta = (order.side == OrderSide::Buy) ? rawPrice * (slippageBps / 10000.0): -rawPrice * (slippageBps / 10000.0);
        
        double executedPrice = rawPrice + slippageDelta;
        double fee = fillQty * commissionPerShare;

        ExecutionFillPayload fill {
            ++fillCounter,
            order.orderId,
            order.symbolId,
            order.side,
            executedPrice,
            fillQty,
            fee,
            std::abs(slippageDelta)
        };
        queue.emplace(timestamp, std::move(fill));
    }

    void evaluatePendingOrders(uint32_t symbolId, const MarketDataPayload& md, EventQueue& queue, int64_t timestamp) {
        auto it = pendingOrders.find(symbolId);
        if (it == pendingOrders.end() || it->second.empty()) {
            return;
        }

        auto& orders = it->second;
        for (auto orderIt = orders.begin(); orderIt != orders.end(); ) {
            bool executed = false;
            const auto& order = *orderIt;

            if (order.type == OrderType::Limit) {
                if (order.side == OrderSide::Buy && md.askPrice > 0.0 && md.askPrice <= order.limitPrice) {
                    double availQty = (md.askSize > 0) ? static_cast<double>(md.askSize) : order.quantity;
                    double fillQty = std::min(order.quantity, availQty);
                    generateFill(order, md.askPrice, fillQty, queue, timestamp);
                    executed = true;
                }
                else if (order.side == OrderSide::Sell && md.bidPrice > 0.0 && md.bidPrice >= order.limitPrice) {
                    double availQty = (md.bidSize > 0) ? static_cast<double>(md.bidSize) : order.quantity;
                    double fillQty = std::min(order.quantity, availQty);
                    generateFill(order, md.bidPrice, fillQty, queue, timestamp);
                    executed = true;
                }
            } 
            else if (order.type == OrderType::Stop) {
                if (order.side == OrderSide::Buy && md.lastTradePrice >= order.limitPrice) {
                    double fillPrice = (md.askPrice > 0.0) ? md.askPrice : md.lastTradePrice;
                    double availQty = (md.askSize > 0) ? static_cast<double>(md.askSize) : order.quantity;
                    double fillQty = std::min(order.quantity, availQty);
                    generateFill(order, fillPrice, fillQty, queue, timestamp);
                    executed = true;
                }
                else if (order.side == OrderSide::Sell && md.lastTradePrice <= order.limitPrice) {
                    double fillPrice = (md.bidPrice > 0.0) ? md.bidPrice : md.lastTradePrice;
                    double availQty = (md.bidSize > 0) ? static_cast<double>(md.bidSize) : order.quantity;
                    double fillQty = std::min(order.quantity, availQty);
                    generateFill(order, fillPrice, fillQty, queue, timestamp);
                    executed = true;
                }
            }

            if (executed) {
                orderIt = orders.erase(orderIt);
            }
            else {
                ++orderIt;
            }
        }
    }

public:
    SimulatedEngine() = default;

    void setCommissionPerShare(double comm) noexcept { commissionPerShare = comm; }
    void setSlippageBps(double bps) noexcept { slippageBps = bps; }

    void onMarketData(const MarketDataPayload& md, EventQueue& queue, int64_t timestamp) {
        marketCache[md.symbolId] = md;
        evaluatePendingOrders(md.symbolId, md, queue, timestamp);
    }

    void onOrder(const OrderPayload& order, EventQueue& queue, int64_t timestamp) {
        auto cacheIterator = marketCache.find(order.symbolId);
        if (cacheIterator == marketCache.end()) {
            if (order.type != OrderType::Market) {
                pendingOrders[order.symbolId].push_back(order);
            }
            return;
        }

        const auto& md = cacheIterator->second;

        if (order.type == OrderType::Market) {
            double rawPrice = (order.side == OrderSide::Buy) ? md.askPrice : md.bidPrice;
            uint32_t availSize = (order.side == OrderSide::Buy) ? md.askSize : md.bidSize;
            
            if (rawPrice <= 0.0) {
                rawPrice = md.lastTradePrice;
            }
            
            double fillQuantity = std::min(order.quantity, static_cast<double>(availSize > 0 ? availSize : order.quantity));
            generateFill(order, rawPrice, fillQuantity, queue, timestamp);
        } 
        else if (order.type == OrderType::Limit) {
            bool canFillNow = false;
            double rawPrice = 0.0;
            uint32_t availSize = 0;

            if (order.side == OrderSide::Buy && md.askPrice > 0.0 && md.askPrice <= order.limitPrice) {
                canFillNow = true;
                rawPrice = md.askPrice;
                availSize = md.askSize;
            }
            else if (order.side == OrderSide::Sell && md.bidPrice > 0.0 && md.bidPrice >= order.limitPrice) {
                canFillNow = true;
                rawPrice = md.bidPrice;
                availSize = md.bidSize;
            }

            if (canFillNow) {
                double fillQty = std::min(order.quantity, static_cast<double>(availSize > 0 ? availSize : order.quantity));
                generateFill(order, rawPrice, fillQty, queue, timestamp);
            }
            else {
                pendingOrders[order.symbolId].push_back(order);
            }
        } 
        else if (order.type == OrderType::Stop) {
            pendingOrders[order.symbolId].push_back(order);
        }
    }

    void Reset() noexcept {
        marketCache.clear();
        pendingOrders.clear();
        fillCounter = 0;
    }
};
