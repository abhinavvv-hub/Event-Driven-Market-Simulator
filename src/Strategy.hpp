#pragma once

#include "EventQueue.hpp"
#include <cstdint>
#include <deque>
#include <unordered_map>

class IStrategy {
protected:
    uint64_t strategyId;

public:
    explicit IStrategy(uint64_t strategyId) : strategyId(strategyId) {}
    virtual ~IStrategy() = default;

    [[nodiscard]] uint64_t getStrategyId() const noexcept { 
        return strategyId; 
    }

    virtual void onMarketData(const MarketDataPayload& md, EventQueue& queue, int64_t timestamp) = 0;
};

class SmaCrossoverStrategy : public IStrategy {
private:
    std::size_t shortPeriod;
    std::size_t longPeriod;
    double tradeQuantity;

    struct SymbolState {
        std::deque<double> prices;
        double prevFastSma{0.0};
        double prevSlowSma{0.0};
        bool hasInitialized{false};
    };

    std::unordered_map<uint32_t, SymbolState> m_symbolStates;

    static double calculateSma(const std::deque<double>& prices, std::size_t period) {
        double sum = 0.0;
        std::size_t startIdx = prices.size() - period;
        for (std::size_t i = startIdx; i < prices.size(); ++i) {
            sum += prices[i];
        }
        return sum / static_cast<double>(period);
    }

public:
    SmaCrossoverStrategy(uint64_t strategyId, std::size_t shortPeriod = 5, std::size_t longPeriod = 20, double tradeQuantity = 100.0) : IStrategy(strategyId), shortPeriod(shortPeriod), longPeriod(longPeriod), tradeQuantity(tradeQuantity) {}

    void onMarketData(const MarketDataPayload& md, EventQueue& queue, int64_t timestamp) override {
        double price = (md.lastTradePrice > 0.0) ? md.lastTradePrice : (md.bidPrice + md.askPrice) / 2.0;
        if (price <= 0.0) return;

        auto& state = m_symbolStates[md.symbolId];
        state.prices.push_back(price);

        if (state.prices.size() > longPeriod) {
            state.prices.pop_front();
        }

        if (state.prices.size() < longPeriod) {
            return;
        }

        double fastSma = calculateSma(state.prices, shortPeriod);
        double slowSma = calculateSma(state.prices, longPeriod);

        if (state.hasInitialized) {
            if (state.prevFastSma <= state.prevSlowSma && fastSma > slowSma) {
                SignalPayload signal {
                    strategyId,
                    md.symbolId,
                    OrderSide::Buy,
                    tradeQuantity
                };
                queue.emplace(timestamp, std::move(signal));
            }

            if (state.prevFastSma >= state.prevSlowSma && fastSma < slowSma) {
                SignalPayload signal {
                    strategyId,
                    md.symbolId,
                    OrderSide::Sell,
                    tradeQuantity
                };
                queue.emplace(timestamp, std::move(signal));
            }
        }

        state.prevFastSma = fastSma;
        state.prevSlowSma = slowSma;
        state.hasInitialized = true;
    }
};
