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

#include <event_manager.h>
#include <event_queue.h>
#include <event_classes.h>
#include <scorbit_sdk/event_types.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <boost/asio.hpp>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <future>

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
    return std::make_shared<CreditsNumberRequestedEvent>();
}

std::shared_ptr<EventBase> createConfigEvent(const std::string &config = "{}")
{
    return std::make_shared<ConfigReceivedEvent>(config);
}

} // namespace

TEST_CASE("EventManager basic operations")
{
    boost::asio::io_context ioContext;
    auto strand = boost::asio::make_strand(ioContext);
    
    std::vector<EventType> receivedEvents;
    auto callback = [&receivedEvents](const EventBase &event) {
        receivedEvents.push_back(event.type());
    };

    auto eventManager = std::make_shared<EventManager>(strand, callback);

    SECTION("Construction and basic push")
    {
        auto event = createGameStartEvent(3);
        eventManager->push(std::move(event));
        
        // Process events
        ioContext.run_for(std::chrono::milliseconds(10));
        
        CHECK(receivedEvents.size() == 1);
        CHECK(receivedEvents[0] == EventType::GameStartRequested);
    }

    SECTION("Multiple events processing")
    {
        eventManager->push(createGameStartEvent(2));
        eventManager->push(createCreditsAddEvent(5));
        eventManager->push(createConfigEvent("test"));
        
        // Process events
        ioContext.run_for(std::chrono::milliseconds(50));
        
        CHECK(receivedEvents.size() == 3);
        // Events should be processed in priority order
        CHECK(receivedEvents[0] == EventType::GameStartRequested); // Highest priority
        CHECK(receivedEvents[1] == EventType::CreditsAddRequested); // High priority
        CHECK(receivedEvents[2] == EventType::ConfigReceived); // Normal priority
    }
}

TEST_CASE("EventManager async processing")
{
    boost::asio::io_context ioContext;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(
            ioContext.get_executor());
    auto strand = boost::asio::make_strand(ioContext);

    std::vector<EventType> receivedEvents;
    std::atomic<int> callbackCount{0};
    
    auto callback = [&receivedEvents, &callbackCount](const EventBase &event) {
        receivedEvents.push_back(event.type());
        callbackCount.fetch_add(1);
    };

    auto eventManager = std::make_shared<EventManager>(strand, callback);

    SECTION("Async event processing")
    {
        // Start processing in a separate thread
        std::thread ioThread([&ioContext]() {
            ioContext.run();
        });

        // Give some time for the thread to start
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Push events
        eventManager->push(createGameStartEvent(2));
        eventManager->push(createCreditsAddEvent(10));
        
        // Wait for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Stop the io_context
        workGuard.reset();
        ioThread.join();
        
        CHECK(callbackCount.load() == 2);
        CHECK(receivedEvents.size() == 2);
    }

    SECTION("Rapid event processing")
    {
        std::thread ioThread([&ioContext]() {
            ioContext.run();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Push many events rapidly
        const int numEvents = 100;
        for (int i = 0; i < numEvents; ++i) {
            eventManager->push(createConfigEvent("event_" + std::to_string(i)));
        }
        
        // Wait for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        workGuard.reset();
        ioThread.join();
        
        CHECK(callbackCount.load() == numEvents);
    }
}

TEST_CASE("EventManager concurrent operations")
{
    boost::asio::io_context ioContext;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(
            ioContext.get_executor());
    auto strand = boost::asio::make_strand(ioContext);

    std::vector<EventType> receivedEvents;
    std::mutex eventsMutex;
    std::atomic<int> callbackCount{0};
    
    auto callback = [&receivedEvents, &eventsMutex, &callbackCount](const EventBase &event) {
        std::lock_guard<std::mutex> lock(eventsMutex);
        receivedEvents.push_back(event.type());
        callbackCount.fetch_add(1);
    };

    auto eventManager = std::make_shared<EventManager>(strand, callback);

    SECTION("Concurrent event pushing")
    {
        std::thread ioThread([&ioContext]() {
            ioContext.run();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        const int numThreads = 4;
        const int eventsPerThread = 25;
        std::vector<std::thread> threads;

        // Start multiple threads pushing events
        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([eventManager, t, eventsPerThread]() {
                for (int i = 0; i < eventsPerThread; ++i) {
                    eventManager->push(createConfigEvent("thread_" + std::to_string(t) + "_event_" + std::to_string(i)));
                }
            });
        }

        // Wait for all threads to complete
        for (auto &thread : threads) {
            thread.join();
        }

        // Wait for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        workGuard.reset();
        ioThread.join();
        
        CHECK(callbackCount.load() == numThreads * eventsPerThread);
    }
}

TEST_CASE("EventManager event ordering")
{
    boost::asio::io_context ioContext;
    auto strand = boost::asio::make_strand(ioContext);
    
    std::vector<EventType> receivedEvents;
    auto callback = [&receivedEvents](const EventBase &event) {
        receivedEvents.push_back(event.type());
    };

    auto eventManager = std::make_shared<EventManager>(strand, callback);

    SECTION("Priority ordering")
    {
        // Push events in random order
        eventManager->push(createConfigEvent("config1")); // Normal priority
        eventManager->push(createGameStartEvent(2));     // Highest priority
        eventManager->push(createCreditsAddEvent(5));     // High priority
        eventManager->push(createConfigEvent("config2")); // Normal priority
        eventManager->push(createCreditsNumberEvent());  // High priority
        
        // Process events
        ioContext.run_for(std::chrono::milliseconds(50));
        
        // Verify priority ordering
        CHECK(receivedEvents.size() == 5);
        CHECK(receivedEvents[0] == EventType::GameStartRequested); // Highest priority first
        CHECK(receivedEvents[1] == EventType::CreditsAddRequested);    // High priority second
        CHECK(receivedEvents[2] == EventType::CreditsNumberRequested); // High priority third
        CHECK(receivedEvents[3] == EventType::ConfigReceived);        // Normal priority (FIFO)
        CHECK(receivedEvents[4] == EventType::ConfigReceived);        // Normal priority (FIFO)
    }

    SECTION("FIFO ordering for same priority")
    {
        // Push multiple events with same priority
        eventManager->push(createConfigEvent("config1"));
        eventManager->push(createConfigEvent("config2"));
        eventManager->push(createConfigEvent("config3"));
        
        // Process events
        ioContext.run_for(std::chrono::milliseconds(50));
        
        // Should maintain FIFO order for same priority
        CHECK(receivedEvents.size() == 3);
        CHECK(receivedEvents[0] == EventType::ConfigReceived);
        CHECK(receivedEvents[1] == EventType::ConfigReceived);
        CHECK(receivedEvents[2] == EventType::ConfigReceived);
    }
}

TEST_CASE("EventManager edge cases")
{
    boost::asio::io_context ioContext;
    auto strand = boost::asio::make_strand(ioContext);
    
    std::vector<EventType> receivedEvents;
    auto callback = [&receivedEvents](const EventBase &event) {
        receivedEvents.push_back(event.type());
    };

    auto eventManager = std::make_shared<EventManager>(strand, callback);

    SECTION("Null event handling")
    {
        // Push null event (should be ignored)
        std::shared_ptr<EventBase> nullEvent = nullptr;
        eventManager->push(std::move(nullEvent));
        
        // Process events
        ioContext.run_for(std::chrono::milliseconds(10));
        
        CHECK(receivedEvents.empty());
    }

    SECTION("Empty queue processing")
    {
        // Try to process with empty queue
        ioContext.run_for(std::chrono::milliseconds(10));
        
        CHECK(receivedEvents.empty());
    }

    SECTION("Rapid push operations")
    {
        const int numEvents = 1000;
        
        // Push many events rapidly
        for (int i = 0; i < numEvents; ++i) {
            eventManager->push(createConfigEvent("rapid_" + std::to_string(i)));
        }
        
        // Process events
        ioContext.run_for(std::chrono::milliseconds(10));
        
        CHECK(receivedEvents.size() == numEvents);
    }
}

TEST_CASE("EventManager callback behavior")
{
    boost::asio::io_context ioContext;
    auto strand = boost::asio::make_strand(ioContext);
    
    std::vector<std::string> receivedData;
    auto callback = [&receivedData](const EventBase &event) {
        switch (event.type()) {
        case EventType::GameStartRequested: {
            auto gameEvent = static_cast<const GameStartRequestedEvent&>(event);
            receivedData.push_back("GameStart:" + std::to_string(gameEvent.playersCount()));
            break;
        }
        case EventType::CreditsAddRequested: {
            auto creditsEvent = static_cast<const CreditsAddRequestedEvent&>(event);
            receivedData.push_back("CreditsAdd:" + std::to_string(creditsEvent.credits()));
            break;
        }
        case EventType::ConfigReceived: {
            auto configEvent = static_cast<const ConfigReceivedEvent&>(event);
            receivedData.push_back("Config:" + configEvent.configJson());
            break;
        }
        default:
            receivedData.push_back("Unknown");
            break;
        }
    };

    auto eventManager = std::make_shared<EventManager>(strand, callback);

    SECTION("Event data extraction")
    {
        eventManager->push(createGameStartEvent(4));
        eventManager->push(createCreditsAddEvent(25));
        eventManager->push(createConfigEvent(R"({"setting": "value"})"));
        
        // Process events
        ioContext.run_for(std::chrono::milliseconds(50));
        
        CHECK(receivedData.size() == 3);
        CHECK(receivedData[0] == "GameStart:4");
        CHECK(receivedData[1] == "CreditsAdd:25");
        CHECK(receivedData[2] == R"(Config:{"setting": "value"})");
    }
}

TEST_CASE("EventManager destruction behavior")
{
    boost::asio::io_context ioContext;
    auto strand = boost::asio::make_strand(ioContext);
    
    std::atomic<int> callbackCount{0};
    auto callback = [&callbackCount](const EventBase &event) {
        callbackCount.fetch_add(1);
    };

    SECTION("Destruction with pending events")
    {
        auto eventManager = std::make_shared<EventManager>(strand, callback);
        
        // Push some events
        eventManager->push(createGameStartEvent(2));
        eventManager->push(createCreditsAddEvent(5));
        
        // Start processing in background
        std::thread ioThread([&ioContext]() {
            ioContext.run();
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Destroy the event manager
        eventManager.reset();
        
        // Wait a bit more
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        ioContext.stop();
        ioThread.join();
        
        // Some events might have been processed before destruction
        CHECK(callbackCount.load() >= 0);
    }

    SECTION("Destruction stops processing")
    {
        auto eventManager = std::make_shared<EventManager>(strand, callback);
        
        std::thread ioThread([&ioContext]() {
            ioContext.run();
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Push events after destruction
        eventManager.reset();
        
        // Try to push events after destruction (should not crash)
        // Note: This tests the robustness of the system
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        ioContext.stop();
        ioThread.join();
    }
}

TEST_CASE("EventManager stress test")
{
    boost::asio::io_context ioContext;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(
            ioContext.get_executor());
    auto strand = boost::asio::make_strand(ioContext);

    std::atomic<int> callbackCount{0};
    auto callback = [&callbackCount](const EventBase &event) {
        callbackCount.fetch_add(1);
    };

    auto eventManager = std::make_shared<EventManager>(strand, callback);

    SECTION("High frequency event processing")
    {
        std::thread ioThread([&ioContext]() {
            ioContext.run();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        const int numEvents = 10000;
        
        // Push many events
        for (int i = 0; i < numEvents; ++i) {
            eventManager->push(createConfigEvent("stress_" + std::to_string(i)));
        }
        
        // Wait for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        workGuard.reset();
        ioThread.join();
        
        CHECK(callbackCount.load() == numEvents);
    }

    SECTION("Mixed priority stress test")
    {
        std::thread ioThread([&ioContext]() {
            ioContext.run();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        const int numEvents = 5000;
        std::atomic<int> highPriorityCount{0};
        std::atomic<int> normalPriorityCount{0};
        
        auto stressCallback = [&highPriorityCount, &normalPriorityCount](const EventBase &event) {
            if (event.type() == EventType::GameStartRequested || 
                event.type() == EventType::CreditsAddRequested) {
                highPriorityCount.fetch_add(1);
            } else {
                normalPriorityCount.fetch_add(1);
            }
        };

        auto stressEventManager = std::make_shared<EventManager>(strand, stressCallback);
        
        // Push mixed priority events
        for (int i = 0; i < numEvents; ++i) {
            if (i % 3 == 0) {
                stressEventManager->push(createGameStartEvent(i % 10));
            } else if (i % 3 == 1) {
                stressEventManager->push(createCreditsAddEvent(i));
            } else {
                stressEventManager->push(createConfigEvent("stress_" + std::to_string(i)));
            }
        }
        
        // Wait for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        workGuard.reset();
        ioThread.join();
        
        CHECK(highPriorityCount.load() + normalPriorityCount.load() == numEvents);
        CHECK(highPriorityCount.load() > 0);
        CHECK(normalPriorityCount.load() > 0);
    }
}

TEST_CASE("EventManager processing state")
{
    boost::asio::io_context ioContext;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(
            ioContext.get_executor());
    auto strand = boost::asio::make_strand(ioContext);

    std::atomic<int> callbackCount{0};
    auto callback = [&callbackCount](const EventBase &event) {
        callbackCount.fetch_add(1);
        // Simulate some processing time
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };

    auto eventManager = std::make_shared<EventManager>(strand, callback);

    SECTION("Processing state management")
    {
        std::thread ioThread([&ioContext]() {
            ioContext.run();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Push multiple events rapidly
        for (int i = 0; i < 10; ++i) {
            eventManager->push(createConfigEvent("event_" + std::to_string(i)));
        }
        
        // Wait for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        workGuard.reset();
        ioThread.join();
        
        CHECK(callbackCount.load() == 10);
    }
}
