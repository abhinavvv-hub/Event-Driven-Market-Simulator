import random

def generate_csv(filename="ticks.csv", num_ticks=200):
    price = 150.0
    timestamp = 1700000000000
    with open(filename, "w") as f:
        f.write("timestamp,symbol,bidPrice,askPrice,lastTradePrice,bidSize,askSize,lastTradeVolume\n")
        for _ in range(num_ticks):
            price += round(random.uniform(-0.5, 0.52), 2)
            bid = round(price - 0.02, 2)
            ask = round(price + 0.02, 2)
            bid_size = random.randint(10, 50) * 10
            ask_size = random.randint(10, 50) * 10
            last_vol = random.randint(5, 30) * 10
            
            f.write(f"{timestamp},AAPL,{bid:.2f},{ask:.2f},{price:.2f},{bid_size},{ask_size},{last_vol}\n")
            timestamp += 1000  # Advance timestamp by 1 second

if __name__ == "__main__":
    generate_csv()
    print("Generated 200 ticks in ticks.csv")
