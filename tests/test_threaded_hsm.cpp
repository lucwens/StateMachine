/**
 * @file test_threaded_hsm.cpp
 * @brief Unit tests for ThreadedHSM threading and message passing
 */

#include <gtest/gtest.h>
#include "../ThreadedHSM.hpp"
#include <atomic>
#include <thread>
#include <vector>

using namespace LaserTracker;

class ThreadedHSMTest : public ::testing::Test
{
  protected:
    std::unique_ptr<ThreadedHSM> hsm;

    void SetUp() override
    {
        hsm = std::make_unique<ThreadedHSM>();
    }

    void TearDown() override
    {
        if (hsm)
        {
            hsm->stop();
        }
    }
};

// ============================================================================
// Lifecycle Tests
// ============================================================================

TEST_F(ThreadedHSMTest, StartsAndStopsCleanly)
{
    hsm->start();
    EXPECT_TRUE(hsm->isRunning());
    hsm->stop();
    EXPECT_FALSE(hsm->isRunning());
}

TEST_F(ThreadedHSMTest, MultipleStartCallsAreSafe)
{
    hsm->start();
    hsm->start(); // Should be idempotent
    EXPECT_TRUE(hsm->isRunning());
}

TEST_F(ThreadedHSMTest, MultipleStopCallsAreSafe)
{
    hsm->start();
    hsm->stop();
    hsm->stop(); // Should be idempotent
    EXPECT_FALSE(hsm->isRunning());
}

TEST_F(ThreadedHSMTest, StopWithoutStartIsSafe)
{
    hsm->stop(); // Should not crash
    EXPECT_FALSE(hsm->isRunning());
}

// ============================================================================
// Async Command Tests
// ============================================================================

TEST_F(ThreadedHSMTest, SendCommandAsyncReturnsMessageId)
{
    hsm->start();
    uint64_t id = hsm->sendMessageAsync(Commands::PowerOn{});
    EXPECT_GT(id, 0u);
}

TEST_F(ThreadedHSMTest, AsyncCommandsProcessed)
{
    hsm->start();
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Initializing");
}

TEST_F(ThreadedHSMTest, MultipleAsyncMessagesProcessedInOrder)
{
    hsm->start();
    hsm->sendMessageAsync(Commands::PowerOn{});
    hsm->sendMessageAsync(Events::InitComplete{});
    hsm->sendMessageAsync(Commands::StartSearch{});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Tracking::Searching");
}

// ============================================================================
// Sync Command Tests
// ============================================================================

TEST_F(ThreadedHSMTest, SendCommandSyncReturnsResponse)
{
    hsm->start();
    auto response = hsm->sendMessage(Commands::PowerOn{});
    EXPECT_TRUE(response.success);
}

TEST_F(ThreadedHSMTest, SyncCommandWaitsForCompletion)
{
    hsm->start();
    auto response = hsm->sendMessage(Commands::PowerOn{});
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Initializing");
}

TEST_F(ThreadedHSMTest, SyncCommandReturnsCorrectState)
{
    hsm->start();
    hsm->sendMessage(Commands::PowerOn{});
    hsm->sendMessage(Events::InitComplete{});
    auto response = hsm->sendMessage(Commands::StartSearch{});
    EXPECT_TRUE(response.success);
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Tracking::Searching");
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(ThreadedHSMTest, ConcurrentAsyncEventsAreSafe)
{
    hsm->start();
    hsm->sendMessage(Commands::PowerOn{});
    hsm->sendMessage(Events::InitComplete{});
    hsm->sendMessage(Commands::StartSearch{});
    hsm->sendMessage(Events::TargetFound{5000.0});
    hsm->sendMessage(Commands::StartMeasure{});

    std::atomic<int> eventsSent{0};
    std::vector<std::thread> threads;

    // Multiple threads sending measurement events
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back(
            [this, &eventsSent, t]()
            {
                for (int i = 0; i < 10; ++i)
                {
                    hsm->sendMessageAsync(Events::MeasurementComplete{
                        static_cast<double>(t * 100 + i),
                        static_cast<double>(t * 200 + i),
                        static_cast<double>(t * 50 + i)});
                    eventsSent++;
                }
            });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    EXPECT_EQ(eventsSent.load(), 40);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // Should still be in Measuring state
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Tracking::Measuring");
}

TEST_F(ThreadedHSMTest, GetCurrentStateNameIsThreadSafe)
{
    hsm->start();
    hsm->sendMessage(Commands::PowerOn{});

    std::atomic<bool> running{true};
    std::vector<std::thread> readers;

    // Multiple threads reading state
    for (int i = 0; i < 4; ++i)
    {
        readers.emplace_back(
            [this, &running]()
            {
                while (running)
                {
                    std::string state = hsm->getCurrentStateName();
                    EXPECT_FALSE(state.empty());
                }
            });
    }

    // Change state while readers are running
    hsm->sendMessage(Events::InitComplete{});
    hsm->sendMessage(Commands::StartSearch{});
    hsm->sendMessage(Events::TargetFound{5000.0});

    running = false;
    for (auto& t : readers)
    {
        t.join();
    }
}

// ============================================================================
// JSON Message Protocol Tests
// ============================================================================

TEST_F(ThreadedHSMTest, SendJsonMessageParsesCorrectly)
{
    hsm->start();
    std::string json = R"({"id": 100, "name": "PowerOn", "sync": false})";
    hsm->sendJsonMessage(json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Initializing");
}

TEST_F(ThreadedHSMTest, SendJsonMessageWithParams)
{
    hsm->start();
    hsm->sendMessage(Commands::PowerOn{});
    hsm->sendMessage(Events::InitComplete{});
    hsm->sendMessage(Commands::StartSearch{});

    std::string json = R"({"id": 101, "name": "TargetFound", "params": {"distance_mm": 3000.0}, "sync": false})";
    hsm->sendJsonMessage(json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Tracking::Locked");
}

// ============================================================================
// Response Queue Tests
// ============================================================================

TEST_F(ThreadedHSMTest, TryGetResponseReturnsNulloptWhenEmpty)
{
    hsm->start();
    auto response = hsm->tryGetResponse();
    EXPECT_FALSE(response.has_value());
}

TEST_F(ThreadedHSMTest, ResponsesAreQueued)
{
    hsm->start();
    std::string json = R"({"id": 200, "name": "PowerOn", "sync": false, "needsReply": true})";
    hsm->sendJsonMessage(json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto response = hsm->tryGetResponse();
    EXPECT_TRUE(response.has_value());
    EXPECT_EQ(response->id, 200u);
    EXPECT_TRUE(response->success);
}

// ============================================================================
// Message Structure Tests
// ============================================================================

TEST_F(ThreadedHSMTest, MessageToJsonWorks)
{
    Message msg;
    msg.id = 123;
    msg.name = "TestCommand";
    msg.success = true;
    msg.params["value"] = 42;

    std::string json = msg.toJson();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("123"), std::string::npos);
    EXPECT_NE(json.find("TestCommand"), std::string::npos);
}

TEST_F(ThreadedHSMTest, MessageCreateResponseWorks)
{
    Json result;
    result["status"] = "ok";

    auto response = Message::createResponse(999, true, result);
    EXPECT_EQ(response.id, 999u);
    EXPECT_TRUE(response.success);
    EXPECT_EQ(response.params["status"], "ok");
}

TEST_F(ThreadedHSMTest, MessageCreateErrorResponseWorks)
{
    auto response = Message::createResponse(888, false, Json{}, "Something went wrong");
    EXPECT_EQ(response.id, 888u);
    EXPECT_FALSE(response.success);
    EXPECT_EQ(response.error, "Something went wrong");
}

// ============================================================================
// State Query Tests
// ============================================================================

TEST_F(ThreadedHSMTest, GetCurrentStateNameAfterStart)
{
    hsm->start();
    EXPECT_EQ(hsm->getCurrentStateName(), "Off");
}

TEST_F(ThreadedHSMTest, StateUpdatesVisibleImmediatelyAfterSync)
{
    hsm->start();
    hsm->sendMessage(Commands::PowerOn{});
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Initializing");

    hsm->sendMessage(Events::InitComplete{});
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Idle");
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(ThreadedHSMTest, CommandBeforeStartIsHandled)
{
    // Should not crash, but command may not be processed
    hsm->sendMessageAsync(Commands::PowerOn{});
    hsm->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // State depends on implementation - may or may not have processed the command
}

TEST_F(ThreadedHSMTest, RapidStartStopCycles)
{
    for (int i = 0; i < 5; ++i)
    {
        hsm->start();
        EXPECT_TRUE(hsm->isRunning());
        hsm->stop();
        EXPECT_FALSE(hsm->isRunning());
    }
}
