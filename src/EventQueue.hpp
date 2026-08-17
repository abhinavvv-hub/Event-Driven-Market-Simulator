#pragma once

#include <cstddef>
#include <queue>
#include <vector>
#include <variant>
#include <utility>
#include <cstdint>
#include <optional>


enum class OrderSide : uint8_t { Buy, Sell };
enum class OrderType : uint8_t { Market, Limit, Stop };

struct MarketDataPayload {
    uint32_t symbolId;
    double bidPrice;
    double askPrice;
    double lastTradePrice;
    uint32_t bidSize;
    uint32_t askSize;
    uint32_t lastTradeVolume;
};
struct SignalPayload {
    uint64_t strategyId;
    uint32_t symbolId;
    OrderSide side;
    double targetQuantity;
};
struct OrderPayload {
    uint64_t orderId;
    uint64_t strategyId;
    uint32_t symbolId;
    OrderSide side;
    OrderType type;
    double limitPrice;
    double quantity;
};
struct ExecutionFillPayload {
    uint64_t fillId;
    uint64_t orderId;
    uint32_t symbolId;
    OrderSide side;
    double filledPrice;
    double filledQuantity;
    double fee;
    double slippage;
};

using EventPayload = std::variant<MarketDataPayload, SignalPayload, OrderPayload, ExecutionFillPayload>;

struct Node {
    int64_t timeStamp;
    uint64_t sequenceNumber;
    EventPayload payload;
    Node(int64_t ts, uint64_t sn, EventPayload p) noexcept : timeStamp(ts), sequenceNumber(sn), payload(std::move(p)) {}
};

class EventQueue {
private:
    struct Comparator {
        bool operator()(Node const &a, Node const &b) const noexcept {
            if (a.timeStamp != b.timeStamp) {
                return a.timeStamp > b.timeStamp;
            }
            else {
                return a.sequenceNumber > b.sequenceNumber;
            }
        }
    };

    class PriorityQueue : public std::priority_queue<Node, std::vector<Node>, Comparator> {
    public:
        void reserveThis(std::size_t Capacity) {
            this->c.reserve(Capacity);
        }
        void clearThis() {
            this->c.clear();
        }
    };

    PriorityQueue pq;
    uint64_t sequenceCounter{0};
public:
    explicit EventQueue() = default;

    [[nodiscard]] std::size_t Size() const noexcept {
        return pq.size();
    }

    [[nodiscard]] bool isEmpty() const noexcept {
        return pq.empty();
    }

    void Reserve(std::size_t Capacity) {
        pq.reserveThis(Capacity);
    }

    void push(int64_t timestamp, EventPayload payload) {
        sequenceCounter += 1;
        pq.emplace(timestamp, sequenceCounter, std::move(payload));
    }

    template <typename T>
    void emplace(int64_t timestamp, T&& specificPayload) {
        sequenceCounter += 1;
        pq.emplace(timestamp, sequenceCounter, EventPayload(std::forward<T>(specificPayload)));
    }

    [[nodiscard]] const Node& Top() const{
        return pq.top();
    }

    bool popTo(Node& outNode) {
        if (pq.empty()) {
            return false;
        }
        outNode = std::move(const_cast<Node&>(pq.top()));
        pq.pop();
        return true;
    }

    [[nodiscard]] std::optional<Node> Pop() {
        if (pq.empty()) {
            return std::nullopt;
        }
        Node topNode = std::move(const_cast<Node&>(pq.top()));
        pq.pop();
        return topNode;
    }

    void Clear() noexcept {
        pq.clearThis();
        sequenceCounter = 0;
    }
};
