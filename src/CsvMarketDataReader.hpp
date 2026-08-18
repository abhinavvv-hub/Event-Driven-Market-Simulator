#pragma once

#include "EventQueue.hpp"
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>

class CsvMarketDataReader {
private:
    std::unordered_map<std::string, uint32_t> symbolToId;
    std::unordered_map<uint32_t, std::string> idToSymbol;
    uint32_t nextSymbolId{1};

    template <std::size_t N> static bool split(std::string_view line, std::array<std::string_view, N>& tokens, char delimiter = ',') {
        size_t start = 0;
        size_t end = line.find(delimiter);
        size_t count = 0;

        while (end != std::string_view::npos && count < N - 1) {
            tokens[count++] = line.substr(start, end - start);
            start = end + 1;
            end = line.find(delimiter, start);
        }
        if (count < N - 1) return false;
        tokens[count] = line.substr(start);
        return true;
    }

    static int64_t parse_Int64(std::string_view sv) noexcept {
        int64_t val = 0;
        std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

    static uint32_t parse_Uint32(std::string_view sv) noexcept {
        uint32_t val = 0;
        std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

    static double parse_Double(std::string_view sv) noexcept {
        double val = 0.0;
        std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

public:
    explicit CsvMarketDataReader() = default;

    uint32_t getSymbolId(std::string_view symbol) {
        std::string symbolStr(symbol);
        auto it = symbolToId.find(symbolStr);
        if (it != symbolToId.end()) {
            return it->second;
        }
        uint32_t newId = nextSymbolId++;
        symbolToId[symbolStr] = newId;
        idToSymbol[newId] = symbolStr;
        return newId;
    }

    [[nodiscard]] std::string_view getSymbol(uint32_t id) const noexcept {
        auto it = idToSymbol.find(id);
        return (it != idToSymbol.end()) ? std::string_view(it->second) : "UNKNOWN";
    }

    std::size_t ingestFile(const std::string& filePath, EventQueue& queue, bool skipHeader = true) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open market data file: " << filePath << '\n';
            return 0;
        }

        std::string line;
        std::size_t recordCount = 0;
        std::array<std::string_view, 8> tokens;

        if (skipHeader && std::getline(file, line)) {}

        while (std::getline(file, line)) {
            if (line.empty()) continue;

            std::string_view sv(line);
            if (!sv.empty() && sv.back() == '\r') {
                sv.remove_suffix(1);
            }

            if (!split<8>(sv, tokens, ',')) {
                continue;
            }

            int64_t timestamp        = parse_Int64(tokens[0]);
            uint32_t symbolId        = getSymbolId(tokens[1]);
            double bidPrice          = parse_Double(tokens[2]);
            double askPrice          = parse_Double(tokens[3]);
            double lastTradePrice    = parse_Double(tokens[4]);
            uint32_t bidSize         = parse_Uint32(tokens[5]);
            uint32_t askSize         = parse_Uint32(tokens[6]);
            uint32_t lastTradeVolume = parse_Uint32(tokens[7]);

            MarketDataPayload payload {
                symbolId,
                bidPrice,
                askPrice,
                lastTradePrice,
                bidSize,
                askSize,
                lastTradeVolume
            };

            queue.emplace(timestamp, std::move(payload));
            ++recordCount;
        }

        return recordCount;
    }
};
