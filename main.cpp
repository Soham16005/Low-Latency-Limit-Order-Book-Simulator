#include <bits/stdc++.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

using namespace std;
using ll = long long;

struct Order {
    ll id;
    ll price;       // price in ticks, e.g. 10000 = $100.00
    int quantity;
    bool is_buy;
};

class OrderBook {
private:
    // Highest bid first, lowest ask first
    map<ll, deque<Order>, greater<ll>> bids;
    map<ll, deque<Order>> asks;

    ll trades = 0;
    ll traded_quantity = 0;

public:
    void add_order(Order order) {
        if (order.is_buy) {

            // Match against lowest-priced asks
            while (order.quantity > 0 && !asks.empty()) {
                auto it = asks.begin();

                // Best ask is too expensive
                if (it->first > order.price)
                    break;

                Order& sell = it->second.front();

                int qty = min(order.quantity, sell.quantity);

                trades++;
                traded_quantity += qty;

                order.quantity -= qty;
                sell.quantity -= qty;

                if (sell.quantity == 0)
                    it->second.pop_front();

                if (it->second.empty())
                    asks.erase(it);
            }

            // Remaining quantity becomes a resting bid
            if (order.quantity > 0)
                bids[order.price].push_back(order);

        } else {

            // Match against highest-priced bids
            while (order.quantity > 0 && !bids.empty()) {
                auto it = bids.begin();

                // Best bid is too cheap
                if (it->first < order.price)
                    break;

                Order& buy = it->second.front();

                int qty = min(order.quantity, buy.quantity);

                trades++;
                traded_quantity += qty;

                order.quantity -= qty;
                buy.quantity -= qty;

                if (buy.quantity == 0)
                    it->second.pop_front();

                if (it->second.empty())
                    bids.erase(it);
            }

            // Remaining quantity becomes a resting ask
            if (order.quantity > 0)
                asks[order.price].push_back(order);
        }
    }

    ll get_trades() const {
        return trades;
    }

    ll get_traded_quantity() const {
        return traded_quantity;
    }
};


class MutexQueue {
private:
    queue<Order> q;
    mutex mtx;

public:
    bool push(const Order& order) {
        lock_guard<mutex> lock(mtx);
        q.push(order);
        return true;
    }

    bool pop(Order& order) {
        lock_guard<mutex> lock(mtx);

        if (q.empty())
            return false;

        order = q.front();
        q.pop();

        return true;
    }
};


template <typename T, size_t SIZE>
class SPSCQueue {
private:
    array<T, SIZE> buffer;

    alignas(64) atomic<size_t> head{0};
    alignas(64) atomic<size_t> tail{0};

public:
    bool push(const T& value) {
        size_t t = tail.load(memory_order_relaxed);

        size_t next = (t + 1) % SIZE;

        size_t h = head.load(memory_order_acquire);

        if (next == h)
            return false;

        buffer[t] = value;

        tail.store(next, memory_order_release);

        return true;
    }

    bool pop(T& value) {
        size_t h = head.load(memory_order_relaxed);

        size_t t = tail.load(memory_order_acquire);

        if (h == t)
            return false;

        value = buffer[h];

        head.store(
            (h + 1) % SIZE,
            memory_order_release
        );

        return true;
    }
};


void benchmark_mutex() {

    const ll N = 1000000;

    MutexQueue q;
    OrderBook book;

    auto start = chrono::high_resolution_clock::now();

    thread producer([&]() {

        for (ll i = 0; i < N; i++) {

            Order order{
                i,
                10000 + (i % 10),  // $100.00 - $100.09
                10,
                i % 2 == 0
            };

            q.push(order);
        }
    });

    thread consumer([&]() {

        Order order;

        for (ll i = 0; i < N; ) {

            if (q.pop(order)) {

                book.add_order(order);

                i++;
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = chrono::high_resolution_clock::now();

    double seconds =
        chrono::duration<double>(end - start).count();

    cout << "Mutex Queue + Order Book\n";

    cout << "Time: "
         << seconds
         << " seconds\n";

    cout << "Throughput: "
         << N / seconds
         << " orders/sec\n";

    cout << "Trades: "
         << book.get_trades()
         << "\n";

    cout << "Traded quantity: "
         << book.get_traded_quantity()
         << "\n\n";
}


void benchmark_spsc() {

    const ll N = 1000000;

    SPSCQueue<Order, 1 << 16> q;

    OrderBook book;

    auto start = chrono::high_resolution_clock::now();

    thread producer([&]() {

        for (ll i = 0; i < N; i++) {

            Order order{
                i,
                10000 + (i % 10),
                10,
                i % 2 == 0
            };

            while (!q.push(order))
                this_thread::yield();
        }
    });

    thread consumer([&]() {

        Order order;

        for (ll i = 0; i < N; ) {

            if (q.pop(order)) {

                book.add_order(order);

                i++;
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = chrono::high_resolution_clock::now();

    double seconds =
        chrono::duration<double>(end - start).count();

    cout << "SPSC Lock-Free Queue + Order Book\n";

    cout << "Time: "
         << seconds
         << " seconds\n";

    cout << "Throughput: "
         << N / seconds
         << " orders/sec\n";

    cout << "Trades: "
         << book.get_trades()
         << "\n";

    cout << "Traded quantity: "
         << book.get_traded_quantity()
         << "\n\n";
}


int main() {

    benchmark_mutex();

    benchmark_spsc();

    return 0;
}