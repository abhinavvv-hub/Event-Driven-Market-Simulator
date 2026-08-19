#include "EventQueue.hpp"
#include "CsvMarketDataReader.hpp"
#include "SimulatedEngine.hpp"
#include "PortfolioRiskEngine.hpp"
#include "Strategy.hpp"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <type_traits>

int main(int argc, char* argv[]) {
    std::string csvPath = "ticks.csv";
    if (argc > 1) {
        csvPath = argv[1];
    }

    std::cout << "=====================================================\n";
    std::cout << "      EVENT-DRIVEN QUANTITATIVE BACKTESTER           \n";
    std::cout << "=====================================================\n";

    EventQueue eventQueue;
    CsvMarketDataReader dataReader;
    SimulatedEngine matchingEngine;

    RiskLimits riskLimits{
        .maxPositionSizePct = 0.25,
        .maxAccountLeverage = 2.0,
        .maxOrderValueUSD   = 50000.0,
        .maxDrawdownPct     = 0.15
    };
    PortfolioAndRiskEngine portfolioEngine(100000.0, riskLimits);
    SmaCrossoverStrategy strategy(1, 5, 20, 100.0);

    eventQueue.Reserve(1'000'000);
    std::cout << "\n[1/3] Ingesting market data from: " << csvPath << "...\n";
    
    std::size_t totalIngested = dataReader.ingestFile(csvPath, eventQueue);
    if (totalIngested == 0) {
        std::cerr << "[-] Error: No records ingested. Make sure '" << csvPath << "' exists.\n";
        return 1;
    }
    std::cout << "[+] Ingested " << totalIngested << " market data ticks into EventQueue.\n";

    matchingEngine.setCommissionPerShare(0.0005);
    matchingEngine.setSlippageBps(1.0);

    std::cout << "\n[2/3] Executing event-driven simulation loop...\n";

    uint64_t processedEvents = 0;
    uint64_t signalCount = 0;
    uint64_t orderCount = 0;
    uint64_t fillCount = 0;

    auto startTime = std::chrono::high_resolution_clock::now();

    while (!eventQueue.isEmpty()) {
        auto nodeOpt = eventQueue.Pop();
        if (!nodeOpt) break;

        const auto& node = *nodeOpt;
        ++processedEvents;
        std::visit([&](auto&& payload) {
            using T = std::decay_t<decltype(payload)>;

            if constexpr (std::is_same_v<T, MarketDataPayload>) {
                portfolioEngine.onMarketData(payload);
                matchingEngine.onMarketData(payload, eventQueue, node.timeStamp);
                strategy.onMarketData(payload, eventQueue, node.timeStamp);
            }

            else if constexpr (std::is_same_v<T, SignalPayload>) {
                signalCount++;
                portfolioEngine.processSignal(payload, eventQueue, node.timeStamp);
            }

            else if constexpr (std::is_same_v<T, OrderPayload>) {
                orderCount++;
                matchingEngine.onOrder(payload, eventQueue, node.timeStamp);
            }

            else if constexpr (std::is_same_v<T, ExecutionFillPayload>) {
                fillCount++;
                portfolioEngine.onFill(payload);
            }
        }, node.payload);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double simTimeSec = std::chrono::duration<double>(endTime - startTime).count();
    double ticksPerSec = simTimeSec > 0.0 ? (totalIngested / simTimeSec) : 0.0;

    std::cout << "\n[3/3] Backtest Execution Complete!\n";
    std::cout << "=====================================================\n";
    std::cout << "                  BACKTEST RESULTS                   \n";
    std::cout << "=====================================================\n";
    std::cout << std::fixed << std::setprecision(2);

    std::cout << " Total Events Processed: " << processedEvents << '\n';
    std::cout << " Signals Generated:      " << signalCount << '\n';
    std::cout << " Orders Routed:          " << orderCount << '\n';
    std::cout << " Executed Fills:         " << fillCount << '\n';
    std::cout << " Processing Time:        " << simTimeSec << " s\n";
    std::cout << " Throughput:            " << ticksPerSec << " ticks/sec\n";
    std::cout << "-----------------------------------------------------\n";
    std::cout << " Starting Capital:       $" << 100000.00 << '\n';
    std::cout << " Ending Cash Balance:    $" << portfolioEngine.getCash() << '\n';
    std::cout << " Ending Total Equity:    $" << portfolioEngine.getTotalEquity() << '\n';
    std::cout << " Realized PnL:           $" << portfolioEngine.getTotalRealizedPnL() << '\n';
    std::cout << " Max Peak-to-Trough DD:  "  << (portfolioEngine.getDrawdownPct() * 100.0) << "%\n";
    std::cout << " Circuit Breaker Status: "  << (portfolioEngine.isTradingHalted() ? "HALTED" : "ACTIVE") << '\n';
    std::cout << "=====================================================\n";

    return 0;
}
