#pragma once

#include "EventQueue.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>


struct Position {
    double quantity{0.0};    
    double avgEntryPrice{0.0};
    double currentPrice{0.0};
    double unrealizedPnL{0.0};
    double realizedPnL{0.0};
};

struct RiskLimits {
    double maxPositionSizePct{0.20};
    double maxAccountLeverage{2.0};
    double maxOrderValueUSD{100000.0};
    double maxDrawdownPct{0.15};
    
};

class PortfolioAndRiskEngine {
private:
    double initialCapital{100000.0};
    double cash{100000.0};
    double totalEquity{100000.0};
    double peakEquity{100000.0};
    double totalRealizedPnL{0.0};
    bool tradingHalted{false};

    uint64_t orderCounter{0};
    RiskLimits limits;
    std::unordered_map<uint32_t, Position> positions;

public:
    explicit PortfolioAndRiskEngine(double ic = 100000.0, RiskLimits limits = RiskLimits{}): initialCapital(ic), cash(ic), totalEquity(ic), peakEquity(ic), limits(limits) {}

    [[nodiscard]] double getCash() const noexcept { 
        return cash; 
    }
    
    [[nodiscard]] double getTotalEquity() const noexcept { 
        return totalEquity; 
    }
    
    [[nodiscard]] double getTotalRealizedPnL() const noexcept { 
        return totalRealizedPnL; 
    }
    
    [[nodiscard]] double getDrawdownPct() const noexcept {
        return (peakEquity > 0.0) ? (peakEquity - totalEquity) / peakEquity : 0.0;
    }
    
    [[nodiscard]] bool isTradingHalted() const noexcept { 
        return tradingHalted; 
    }

    [[nodiscard]] const Position* getPosition(uint32_t symbolId) const {
        auto it = positions.find(symbolId);
        return (it != positions.end()) ? &it->second : nullptr;
    }

    void onMarketData(const MarketDataPayload& md) {
        auto& pos = positions[md.symbolId];
        pos.currentPrice = md.lastTradePrice > 0.0 ? md.lastTradePrice : (md.bidPrice + md.askPrice) / 2.0;
        if (pos.quantity != 0.0) {
            pos.unrealizedPnL = pos.quantity * (pos.currentPrice - pos.avgEntryPrice);
        }
        else {
            pos.unrealizedPnL = 0.0;
        }

        double sumUnrealizedPnL = 0.0;
        for (const auto& [sym, position] : positions) {
            sumUnrealizedPnL += position.unrealizedPnL;
        }
        totalEquity = cash + sumUnrealizedPnL;
        if (totalEquity > peakEquity) {
            peakEquity = totalEquity;
        }

        double currentDrawdown = getDrawdownPct();
        if (currentDrawdown >= limits.maxDrawdownPct) {
            tradingHalted = true;
        }
    }

    bool processSignal(const SignalPayload& signal, EventQueue& queue, int64_t timestamp) {
        if (tradingHalted) {
            return false;
        }
        double markPrice = 0.0;
        auto posIt = positions.find(signal.symbolId);
        if (posIt != positions.end() && posIt->second.currentPrice > 0.0) {
            markPrice = posIt->second.currentPrice;
        }

        if (markPrice <= 0.0) {
            return false;
        }
        double orderValue = signal.targetQuantity * markPrice;
        if (orderValue > limits.maxOrderValueUSD) {
            return false;
        }

        double currentPosValue = (posIt != positions.end()) ? std::abs(posIt->second.quantity) * markPrice : 0.0;
        double futurePosValue = currentPosValue + orderValue;
        if ((futurePosValue / totalEquity) > limits.maxPositionSizePct) {
            return false;
        }
        double totalExposure = 0.0;
        for (const auto& [sym, pos] : positions) {
            totalExposure += std::abs(pos.quantity) * pos.currentPrice;
        }
        if (((totalExposure + orderValue) / totalEquity) > limits.maxAccountLeverage) {
            return false;
        }
        OrderPayload order{
            ++orderCounter,
            signal.strategyId,
            signal.symbolId,
            signal.side,
            OrderType::Market,
            markPrice,
            signal.targetQuantity
        };
        queue.emplace(timestamp, std::move(order));
        return true;
    }

    void onFill(const ExecutionFillPayload& fill) {
        auto& pos = positions[fill.symbolId];
        double signedFillQty = (fill.side == OrderSide::Buy) ? fill.filledQuantity : -fill.filledQuantity;
        double tradeValue = fill.filledQuantity * fill.filledPrice;

        cash -= (signedFillQty * fill.filledPrice) + fill.fee;
        if (pos.quantity == 0.0 || (pos.quantity > 0 && signedFillQty > 0) || (pos.quantity < 0 && signedFillQty < 0)) {
            double totalQty = pos.quantity + signedFillQty;
            pos.avgEntryPrice = ((pos.quantity * pos.avgEntryPrice) + (signedFillQty * fill.filledPrice)) / totalQty;
            pos.quantity = totalQty;
        }
        else {
            double closedQty = std::min(std::abs(pos.quantity), std::abs(signedFillQty));
            double pnl = 0.0;
            if (pos.quantity > 0) {
                pnl = closedQty * (fill.filledPrice - pos.avgEntryPrice);
            }
            else {
                pnl = closedQty * (pos.avgEntryPrice - fill.filledPrice);
            }

            pos.realizedPnL += pnl;
            totalRealizedPnL += pnl;

            pos.quantity += signedFillQty;
            if (pos.quantity != 0.0 && ((signedFillQty > 0) != (pos.quantity > 0))) {
                pos.avgEntryPrice = fill.filledPrice;
            }
            else if (pos.quantity == 0.0) {
                pos.avgEntryPrice = 0.0;
            }
        }
        pos.unrealizedPnL = (pos.quantity != 0.0) ? pos.quantity * (fill.filledPrice - pos.avgEntryPrice) : 0.0;
        
        double sumUnrealized = 0.0;
        for (const auto& [sym, position] : positions) {
            sumUnrealized += position.unrealizedPnL;
        }
        totalEquity = cash + sumUnrealized;
    }

    void Reset(double initialCapital) {
        initialCapital = initialCapital;
        cash = initialCapital;
        totalEquity = initialCapital;
        peakEquity = initialCapital;
        totalRealizedPnL = 0.0;
        tradingHalted = false;
        orderCounter = 0;
        positions.clear();
    }
};
