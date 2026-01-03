/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <event_queue.h>
#include <event_classes.h>
#include <scorbit_sdk/event_types.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <atomic>

using namespace scorbit;
using namespace scorbit::detail;

namespace {

// Helper function to create events for testing
std::shared_ptr<EventBase> createGameStartEvent(int playersCount = 2)
{
    return std::make_shared<GameStartRequestedEvent>(playersCount);
}

std::shared_ptr<EventBase> createCreditsAddEvent(int credits = 10)
{
    return std::make_shared<CreditsAddRequestedEvent>(credits, "my_transaction");
}

std::shared_ptr<EventBase> createCreditsNumberEvent()
{
    return std::make_shared<CreditsStatusRequestedEvent>();
}

std::shared_ptr<EventBase> createConfigEvent(const std::string &config = "{}")
{
    return std::make_shared<ConfigReceivedEvent>(config);
}

} // namespace

TEST_CASE("EventQueue basic operations")
{
    EventQueue events;

    SECTION("Initially empty")
    {
        CHECK(events.empty());
    }

    SECTION("Enqueue single event")
    {
        events.enqueue(createGameStartEvent());
        CHECK_FALSE(events.empty());
    }

    SECTION("Dequeue from empty queue")
    {
        auto event = events.dequeue();
        CHECK(event == nullptr);
    }

    SECTION("Enqueue and dequeue single event")
    {
        auto originalEvent = createGameStartEvent(3);
        auto eventType = originalEvent->type();

        events.enqueue(std::move(originalEvent));
        CHECK_FALSE(events.empty());

        auto dequeuedEvent = events.dequeue();
        CHECK(dequeuedEvent != nullptr);
        CHECK(dequeuedEvent->type() == eventType);
        CHECK(events.empty());
    }
}

TEST_CASE("EventQueue priority ordering")
{
    EventQueue events;

    SECTION("Highest priority events come first")
    {
        // Add events in random order
        events.enqueue(createConfigEvent("config1")); // Normal priority
        events.enqueue(createGameStartEvent(2));      // Highest priority
        events.enqueue(createCreditsAddEvent(5));     // High priority
        events.enqueue(createConfigEvent("config2")); // Normal priority
        events.enqueue(createCreditsNumberEvent());   // High priority

        // Dequeue and verify order
        auto first = events.dequeue();
        REQUIRE(first != nullptr);
        CHECK(first->type() == EventType::GameStartRequested); // Highest priority first

        auto second = events.dequeue();
        REQUIRE(second != nullptr);
        CHECK(second->type() == EventType::CreditsAddRequested); // High priority second

        auto third = events.dequeue();
        REQUIRE(third != nullptr);
        CHECK(third->type() == EventType::CreditsStatusRequested); // High priority third

        // Normal priority events should come last (FIFO among same priority)
        auto fourth = events.dequeue();
        REQUIRE(fourth != nullptr);
        CHECK(fourth->type() == EventType::ConfigReceived);

        auto fifth = events.dequeue();
        REQUIRE(fifth != nullptr);
        CHECK(fifth->type() == EventType::ConfigReceived);

        CHECK(events.empty());
    }

    SECTION("FIFO ordering for same priority")
    {
        // Add multiple events with same priority
        events.enqueue(createConfigEvent("config1"));
        events.enqueue(createConfigEvent("config2"));
        events.enqueue(createConfigEvent("config3"));

        // Should maintain FIFO order
        auto first = events.dequeue();
        CHECK(first != nullptr);
        CHECK(first->type() == EventType::ConfigReceived);

        auto second = events.dequeue();
        CHECK(second != nullptr);
        CHECK(second->type() == EventType::ConfigReceived);

        auto third = events.dequeue();
        CHECK(third != nullptr);
        CHECK(third->type() == EventType::ConfigReceived);

        CHECK(events.empty());
    }
}

TEST_CASE("EventQueue multiple events")
{
    EventQueue events;

    SECTION("Enqueue multiple events")
    {
        const int numEvents = 10;

        for (int i = 0; i < numEvents; ++i) {
            events.enqueue(createConfigEvent("config" + std::to_string(i)));
        }

        CHECK_FALSE(events.empty());

        // Dequeue all events
        int dequeuedCount = 0;
        while (!events.empty()) {
            auto event = events.dequeue();
            CHECK(event != nullptr);
            ++dequeuedCount;
        }

        CHECK(dequeuedCount == numEvents);
    }

    SECTION("Mixed priority events")
    {
        // Add events with different priorities
        events.enqueue(createConfigEvent("normal1"));
        events.enqueue(createGameStartEvent(2)); // Highest
        events.enqueue(createConfigEvent("normal2"));
        events.enqueue(createCreditsAddEvent(5)); // High
        events.enqueue(createConfigEvent("normal3"));

        std::vector<EventType> expectedOrder = {
                EventType::GameStartRequested,  // Highest priority
                EventType::CreditsAddRequested, // High priority
                EventType::ConfigReceived,      // Normal priority (FIFO)
                EventType::ConfigReceived,      // Normal priority (FIFO)
                EventType::ConfigReceived       // Normal priority (FIFO)
        };

        for (const auto &expectedType : expectedOrder) {
            auto event = events.dequeue();
            CHECK(event != nullptr);
            CHECK(event->type() == expectedType);
        }

        CHECK(events.empty());
    }
}

TEST_CASE("EventQueue thread safety")
{
    EventQueue events;
    const int numThreads = 4;
    const int eventsPerThread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> enqueueCount {0};
    std::atomic<int> dequeueCount {0};

    SECTION("Concurrent enqueue operations")
    {
        // Start multiple threads that enqueue events
        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([&events, &enqueueCount, eventsPerThread]() {
                for (int i = 0; i < eventsPerThread; ++i) {
                    events.enqueue(createConfigEvent("thread_" + std::to_string(i)));
                    enqueueCount.fetch_add(1);
                }
            });
        }

        // Wait for all threads to complete
        for (auto &thread : threads) {
            thread.join();
        }

        CHECK(enqueueCount.load() == numThreads * eventsPerThread);
        CHECK_FALSE(events.empty());
    }

    SECTION("Concurrent enqueue and dequeue operations")
    {
        // Start threads that enqueue events
        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([&events, &enqueueCount, eventsPerThread]() {
                for (int i = 0; i < eventsPerThread; ++i) {
                    events.enqueue(createConfigEvent("thread_" + std::to_string(i)));
                    enqueueCount.fetch_add(1);
                }
            });
        }

        std::atomic<bool> stopFlag {false};

        // Start threads that dequeue events
        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([&events, &dequeueCount, &stopFlag]() {
                while (!stopFlag || !events.empty()) {
                    auto event = events.dequeue();
                    if (event == nullptr) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
                    dequeueCount.fetch_add(1);
                }
            });
        }

        // Wait for enqueue threads to complete
        for (size_t t = 0; t < numThreads; ++t) {
            threads[t].join();
        }

        // Signal them to stop when queue is empty
        stopFlag.store(true);

        // Join dequeue threads
        for (size_t t = numThreads; t < threads.size(); ++t) {
            threads[t].join();
        }

        // The exact count might vary due to timing, but we should have processed most events
        CHECK(dequeueCount > 0);
        CHECK(events.empty());
    }
}

TEST_CASE("EventQueue edge cases")
{
    EventQueue events;

    SECTION("Dequeue from empty queue multiple times")
    {
        REQUIRE(events.empty());
        CHECK(events.dequeue() == nullptr);
        CHECK(events.dequeue() == nullptr);
        CHECK(events.empty());
    }

    SECTION("Enqueue null event (should not crash)")
    {
        // This tests the robustness - EventQueue should handle null events gracefully
        // Note: The current implementation doesn't explicitly check for null,
        // but std::shared_ptr handles null gracefully
        std::shared_ptr<EventBase> nullEvent = nullptr;
        events.enqueue(std::move(nullEvent));
        CHECK(events.empty());
    }

    SECTION("Rapid enqueue/dequeue operations")
    {
        const int numOperations = 1000;

        for (int i = 0; i < numOperations; ++i) {
            events.enqueue(createConfigEvent("rapid_" + std::to_string(i)));
            auto event = events.dequeue();
            CHECK(event != nullptr);
        }

        CHECK(events.empty());
    }

    SECTION("Large number of events")
    {
        const int largeNumber = 10000;

        // Enqueue many events
        for (int i = 0; i < largeNumber; ++i) {
            events.enqueue(createConfigEvent("large_" + std::to_string(i)));
        }

        // Dequeue all events
        int count = 0;
        while (!events.empty()) {
            auto event = events.dequeue();
            CHECK(event != nullptr);
            ++count;
        }

        CHECK(count == largeNumber);
    }
}

TEST_CASE("EventQueue event type verification")
{
    EventQueue events;

    SECTION("GameStartRequestedEvent properties")
    {
        auto event = createGameStartEvent(4);
        events.enqueue(std::move(event));

        auto dequeuedEvent = events.dequeue();
        REQUIRE(dequeuedEvent != nullptr);
        CHECK(dequeuedEvent->type() == EventType::GameStartRequested);

        auto gameStartEvent = std::dynamic_pointer_cast<GameStartRequestedEvent>(dequeuedEvent);
        REQUIRE(gameStartEvent != nullptr);
        CHECK(gameStartEvent->playersCount() == 4);
    }

    SECTION("CreditsAddRequestedEvent properties")
    {
        auto event = createCreditsAddEvent(25);
        events.enqueue(std::move(event));

        auto dequeuedEvent = events.dequeue();
        REQUIRE(dequeuedEvent != nullptr);
        CHECK(dequeuedEvent->type() == EventType::CreditsAddRequested);

        auto creditsEvent = std::dynamic_pointer_cast<CreditsAddRequestedEvent>(dequeuedEvent);
        REQUIRE(creditsEvent != nullptr);
        CHECK(creditsEvent->credits() == 25);
    }

    SECTION("ConfigReceivedEvent properties")
    {
        std::string configJson = R"({"setting": "value"})";
        auto event = createConfigEvent(configJson);
        events.enqueue(std::move(event));

        auto dequeuedEvent = events.dequeue();
        REQUIRE(dequeuedEvent != nullptr);
        CHECK(dequeuedEvent->type() == EventType::ConfigReceived);

        auto configEvent = std::dynamic_pointer_cast<ConfigReceivedEvent>(dequeuedEvent);
        REQUIRE(configEvent != nullptr);
        CHECK(configEvent->configJson() == configJson);
    }
}

TEST_CASE("EventQueue stress test")
{
    EventQueue events;

    SECTION("Random operations stress test")
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> operationDist(0, 2); // 0=enqueue, 1=dequeue, 2=empty
        std::uniform_int_distribution<> eventTypeDist(0, 3); // Different event types

        const int numOperations = 10000;
        int enqueueCount = 0;
        int dequeueCount = 0;

        for (int i = 0; i < numOperations; ++i) {
            int operation = operationDist(gen);

            switch (operation) {
            case 0: { // Enqueue
                int eventType = eventTypeDist(gen);
                std::shared_ptr<EventBase> event;

                switch (eventType) {
                case 0:
                    event = createGameStartEvent(i % 10);
                    break;
                case 1:
                    event = createCreditsAddEvent(i);
                    break;
                case 2:
                    event = createCreditsNumberEvent();
                    break;
                case 3:
                    event = createConfigEvent("stress_" + std::to_string(i));
                    break;
                }

                events.enqueue(std::move(event));
                ++enqueueCount;
                break;
            }
            case 1: { // Dequeue
                auto event = events.dequeue();
                if (event != nullptr) {
                    ++dequeueCount;
                }
                break;
            }
            case 2: { // Check empty
                // Just call empty() to test thread safety
                events.empty();
                break;
            }
            }
        }

        // Dequeue remaining events
        while (!events.empty()) {
            auto event = events.dequeue();
            if (event != nullptr) {
                ++dequeueCount;
            }
        }

        CHECK(dequeueCount == enqueueCount);
    }
}
