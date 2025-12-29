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

TEST_F(ThreadedHSMTest, SendStateCommandAsyncReturnsMessageId)
{
    hsm->start();
    uint64_t id = hsm->sendStateCommandAsync(StateCommands::PowerOn{});
    EXPECT_GT(id, 0u);
}

TEST_F(ThreadedHSMTest, AsyncCommandsProcessed)
{
    hsm->start();
    hsm->sendStateCommandAsync(StateCommands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Initializing");
}

TEST_F(ThreadedHSMTest, MultipleAsyncCommandsProcessedInOrder)
{
    hsm->start();
    hsm->sendStateCommandAsync(StateCommands::PowerOn{});
    hsm->sendStateCommandAsync(StateCommands::InitComplete{});
    hsm->sendStateCommandAsync(StateCommands::StartSearch{});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Tracking::Searching");
}

// ============================================================================
// Sync Command Tests
// ============================================================================

TEST_F(ThreadedHSMTest, SendStateCommandSyncReturnsResponse)
{
    hsm->start();
    auto response = hsm->sendStateCommandSync(StateCommands::PowerOn{});
    EXPECT_TRUE(response.success);
}

TEST_F(ThreadedHSMTest, SyncCommandWaitsForCompletion)
{
    hsm->start();
    auto response = hsm->sendStateCommandSync(StateCommands::PowerOn{});
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Initializing");
}

TEST_F(ThreadedHSMTest, SyncCommandReturnsCorrectState)
{
    hsm->start();
    hsm->sendStateCommandSync(StateCommands::PowerOn{});
    hsm->sendStateCommandSync(StateCommands::InitComplete{});
    auto response = hsm->sendStateCommandSync(StateCommands::StartSearch{});
    EXPECT_TRUE(response.success);
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Tracking::Searching");
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(ThreadedHSMTest, ConcurrentAsyncCommandsAreSafe)
{
    hsm->start();
    hsm->sendStateCommandSync(StateCommands::PowerOn{});
    hsm->sendStateCommandSync(StateCommands::InitComplete{});
    hsm->sendStateCommandSync(StateCommands::StartSearch{});
    hsm->sendStateCommandSync(StateCommands::TargetFound{5000.0});
    hsm->sendStateCommandSync(StateCommands::StartMeasure{});

    std::atomic<int> commandsSent{0};
    std::vector<std::thread> threads;

    // Multiple threads sending measurement commands
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back(
            [this, &commandsSent, t]()
            {
                for (int i = 0; i < 10; ++i)
                {
                    hsm->sendStateCommandAsync(StateCommands::MeasurementComplete{
                        static_cast<double>(t * 100 + i),
                        static_cast<double>(t * 200 + i),
                        static_cast<double>(t * 50 + i)});
                    commandsSent++;
                }
            });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    EXPECT_EQ(commandsSent.load(), 40);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // Should still be in Measuring state
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Tracking::Measuring");
}

TEST_F(ThreadedHSMTest, GetCurrentStateNameIsThreadSafe)
{
    hsm->start();
    hsm->sendStateCommandSync(StateCommands::PowerOn{});

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
    hsm->sendStateCommandSync(StateCommands::InitComplete{});
    hsm->sendStateCommandSync(StateCommands::StartSearch{});
    hsm->sendStateCommandSync(StateCommands::TargetFound{5000.0});

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
    hsm->sendStateCommandSync(StateCommands::PowerOn{});
    hsm->sendStateCommandSync(StateCommands::InitComplete{});
    hsm->sendStateCommandSync(StateCommands::StartSearch{});

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
    hsm->sendStateCommandSync(StateCommands::PowerOn{});
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Initializing");

    hsm->sendStateCommandSync(StateCommands::InitComplete{});
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Idle");
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(ThreadedHSMTest, CommandBeforeStartIsHandled)
{
    // Should not crash, but command may not be processed
    hsm->sendStateCommandAsync(StateCommands::PowerOn{});
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
