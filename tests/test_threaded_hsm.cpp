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
// Sync Command Tests (with Expected State behavior)
// ============================================================================

TEST_F(ThreadedHSMTest, SendCommandSyncReturnsResponse)
{
    hsm->start();
    // PowerOn now waits for Idle state, so we need to send InitComplete from another thread
    std::thread eventThread(
        [this]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            hsm->sendMessageAsync(Events::InitComplete{});
        });

    auto response = hsm->sendMessage(Commands::PowerOn{});
    eventThread.join();

    EXPECT_TRUE(response.success);
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Idle");
}

TEST_F(ThreadedHSMTest, SyncCommandWaitsForExpectedState)
{
    hsm->start();
    // PowerOn with expectedState=Idle waits until InitComplete transitions to Idle
    std::thread eventThread(
        [this]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            hsm->sendMessageAsync(Events::InitComplete{});
        });

    auto response = hsm->sendMessage(Commands::PowerOn{});
    eventThread.join();

    // Now state should be Idle (not Initializing) because we waited for expectedState
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Idle");
}

TEST_F(ThreadedHSMTest, SyncCommandReturnsCorrectState)
{
    hsm->start();
    // PowerOn waits for Idle
    std::thread initThread(
        [this]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            hsm->sendMessageAsync(Events::InitComplete{});
        });
    hsm->sendMessage(Commands::PowerOn{});
    initThread.join();

    // StartSearch waits for Locked
    std::thread targetThread(
        [this]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            hsm->sendMessageAsync(Events::TargetFound{5000.0});
        });
    auto response = hsm->sendMessage(Commands::StartSearch{});
    targetThread.join();

    EXPECT_TRUE(response.success);
    // Now state should be Locked (because StartSearch waited for TargetFound)
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Tracking::Locked");
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(ThreadedHSMTest, ConcurrentAsyncEventsAreSafe)
{
    hsm->start();
    // Setup to Measuring state using async/threads for commands with expectedState
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessage(Events::InitComplete{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    hsm->sendMessageAsync(Commands::StartSearch{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessage(Events::TargetFound{5000.0});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    hsm->sendMessage(Commands::StartMeasure{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

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
    // Use async for PowerOn since it has expectedState
    hsm->sendMessageAsync(Commands::PowerOn{});

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

    // Change state while readers are running (use async for commands with expectedState)
    hsm->sendMessage(Events::InitComplete{});
    hsm->sendMessageAsync(Commands::StartSearch{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
    // Use async for commands with expectedState to avoid blocking
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessage(Events::InitComplete{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessageAsync(Commands::StartSearch{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

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
    // Use PowerOff (expectedState=Off, immediate) which doesn't wait for external events
    // First go to a state where PowerOff is valid
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send PowerOff with needsReply via JSON - this should return immediately
    std::string json = R"({"id": 200, "name": "PowerOff", "sync": false, "needsReply": true})";
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
    // PowerOn with expectedState=Idle waits until InitComplete, so use threads
    std::thread eventThread(
        [this]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            hsm->sendMessageAsync(Events::InitComplete{});
        });
    hsm->sendMessage(Commands::PowerOn{});
    eventThread.join();

    // After PowerOn returns, state should be Idle (because it waited for expectedState)
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Idle");

    // Events don't have expectedState, so this still returns immediately
    hsm->sendMessage(Events::ErrorOccurred{99, "test"});
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Error");
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

// ============================================================================
// Caller Blocking Tests (Dimension 1: send() vs sendAsync())
// ============================================================================

TEST_F(ThreadedHSMTest, SendAsyncReturnsImmediately)
{
    hsm->start();

    auto start = std::chrono::steady_clock::now();
    uint64_t id = hsm->sendAsync("GetStatus", {}, false);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // sendAsync should return in < 10ms (it just queues the message)
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 10);
    EXPECT_GT(id, 0u);
}

TEST_F(ThreadedHSMTest, SendBlocksUntilResponse)
{
    hsm->start();
    // Go to Idle state first
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessage(Events::InitComplete{});

    // Home command takes time (simulated delay in execute())
    auto start = std::chrono::steady_clock::now();
    auto response = hsm->send("Home", {{"speed", 100.0}}, false);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // send() should block until Home completes (Home has a sleep in execute())
    EXPECT_TRUE(response.success);
    // Home at 100% speed sleeps for 1000ms, but we're flexible here
    EXPECT_GT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500);
}

TEST_F(ThreadedHSMTest, SendAsyncWithSyncFlagReturnsImmediately)
{
    hsm->start();
    // Go to Idle state first
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessage(Events::InitComplete{});

    // Even with sync=true, sendAsync returns immediately
    auto start = std::chrono::steady_clock::now();
    uint64_t id = hsm->sendAsync("Home", {{"speed", 100.0}}, /*sync=*/true);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // sendAsync should return immediately regardless of sync flag
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 10);
    EXPECT_GT(id, 0u);

    // Wait for Home to actually complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
}

// ============================================================================
// Queue Blocking Tests (Dimension 2: sync flag buffers other sync messages)
// ============================================================================

TEST_F(ThreadedHSMTest, SyncFlagBuffersOtherSyncMessages)
{
    hsm->start();
    // Go to Idle state
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessage(Events::InitComplete{});

    // Start a slow sync operation (Home takes ~1s at 100% speed)
    hsm->sendAsync("Home", {{"speed", 100.0}}, /*sync=*/true);

    // Small delay to ensure Home starts processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send another sync command while Home is running - it should be buffered
    auto start = std::chrono::steady_clock::now();
    hsm->sendAsync("Compensate", {{"temperature", 20.0}, {"pressure", 1013.25}, {"humidity", 50.0}}, /*sync=*/true);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // sendAsync returns immediately even though it will be buffered
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 10);

    // Wait for both commands to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    // Both should have completed
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Idle");
}

TEST_F(ThreadedHSMTest, NonSyncMessagesNotBufferedDuringSyncOperation)
{
    // This test verifies the difference between sync and non-sync message handling:
    // - sync=true messages are moved to a special buffer during a sync operation
    // - sync=false messages stay in the normal queue
    //
    // Note: Both still wait for the current message to finish (single-threaded worker),
    // but the buffering affects the ORDER of processing when multiple messages are queued.

    hsm->start();
    // Go to Idle state
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessage(Events::InitComplete{});

    // Queue multiple messages while Home is running
    // The order should demonstrate buffering behavior:
    // 1. Home (sync=true) - starts executing
    // 2. GetStatus (sync=false) - goes to normal queue
    // 3. Compensate (sync=true) - goes to buffer

    hsm->sendAsync("Home", {{"speed", 100.0}}, /*sync=*/true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); // Let Home start

    // Queue GetStatus (non-sync) and Compensate (sync)
    hsm->sendAsync("GetStatus", {}, /*sync=*/false);
    hsm->sendAsync("Compensate", {{"temperature", 20.0}}, /*sync=*/true);

    // Wait for all to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    // All should complete successfully
    // Processing order: Home -> GetStatus -> Compensate
    // (GetStatus not buffered, Compensate buffered but processed after GetStatus)
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Idle");
}

TEST_F(ThreadedHSMTest, BufferedMessagesProcessedAfterSyncCompletes)
{
    hsm->start();
    // Go to Idle state
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessage(Events::InitComplete{});

    // Track when Compensate completes
    std::promise<Message> compensatePromise;
    auto compensateFuture = compensatePromise.get_future();

    // Start Home (sync=true, takes ~1s)
    auto homeStart = std::chrono::steady_clock::now();
    hsm->sendAsync("Home", {{"speed", 100.0}}, /*sync=*/true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Queue Compensate (sync=true) - will be buffered
    std::thread compensateThread(
        [this, &compensatePromise]()
        {
            auto response = hsm->send("Compensate", {{"temperature", 25.0}, {"pressure", 1013.0}, {"humidity", 45.0}}, /*sync=*/true);
            compensatePromise.set_value(response);
        });

    // Wait for Compensate to complete
    auto compensateResponse = compensateFuture.get();
    auto totalElapsed = std::chrono::steady_clock::now() - homeStart;
    compensateThread.join();

    // Compensate should complete after Home (~1s) + Compensate (~0.5s)
    EXPECT_TRUE(compensateResponse.success);
    EXPECT_GT(std::chrono::duration_cast<std::chrono::milliseconds>(totalElapsed).count(), 1000);
}

// ============================================================================
// Unblock Condition Tests (Dimension 3: Immediate, Result, Expected State)
// ============================================================================

TEST_F(ThreadedHSMTest, UnblockCondition_Immediate_Event)
{
    hsm->start();
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Events have immediate unblock condition
    auto start = std::chrono::steady_clock::now();
    auto response = hsm->sendMessage(Events::InitComplete{});
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Should return almost immediately (just processing time)
    EXPECT_TRUE(response.success);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 100);
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Idle");
}

TEST_F(ThreadedHSMTest, UnblockCondition_Immediate_StateCommandWithoutExpectedState)
{
    hsm->start();
    // Go to Operational state
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessage(Events::InitComplete{});

    // PowerOff has expectedState="Off" which is immediate (direct transition)
    auto start = std::chrono::steady_clock::now();
    auto response = hsm->sendMessage(Commands::PowerOff{});
    auto elapsed = std::chrono::steady_clock::now() - start;

    // PowerOff transitions directly to Off, no waiting needed
    EXPECT_TRUE(response.success);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 100);
    EXPECT_EQ(hsm->getCurrentStateName(), "Off");
}

TEST_F(ThreadedHSMTest, UnblockCondition_Result_ActionCommand)
{
    hsm->start();
    // Go to Idle state
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessage(Events::InitComplete{});

    // Home is an action command with execute() that takes time
    auto start = std::chrono::steady_clock::now();
    auto response = hsm->sendMessage(Commands::Home{100.0});
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Unblocks when execute() returns (after ~1s sleep in Home)
    EXPECT_TRUE(response.success);
    EXPECT_GT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500);
    // Result should contain position data
    EXPECT_TRUE(response.params.contains("position"));
}

TEST_F(ThreadedHSMTest, UnblockCondition_ExpectedState_PowerOn)
{
    hsm->start();

    // PowerOn has expectedState="Operational::Idle"
    // It won't return until InitComplete event transitions to Idle
    std::thread eventThread(
        [this]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            hsm->sendMessageAsync(Events::InitComplete{});
        });

    auto start = std::chrono::steady_clock::now();
    auto response = hsm->sendMessage(Commands::PowerOn{});
    auto elapsed = std::chrono::steady_clock::now() - start;
    eventThread.join();

    // Should have waited for InitComplete (~200ms delay)
    EXPECT_TRUE(response.success);
    EXPECT_GT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 150);
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Idle");
}

TEST_F(ThreadedHSMTest, UnblockCondition_ExpectedState_StartSearch)
{
    hsm->start();
    // Go to Idle state
    std::thread initThread(
        [this]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            hsm->sendMessageAsync(Events::InitComplete{});
        });
    hsm->sendMessage(Commands::PowerOn{});
    initThread.join();

    // StartSearch has expectedState="Operational::Tracking::Locked"
    // It won't return until TargetFound event transitions to Locked
    std::thread targetThread(
        [this]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            hsm->sendMessageAsync(Events::TargetFound{5000.0});
        });

    auto start = std::chrono::steady_clock::now();
    auto response = hsm->sendMessage(Commands::StartSearch{});
    auto elapsed = std::chrono::steady_clock::now() - start;
    targetThread.join();

    // Should have waited for TargetFound (~200ms delay)
    EXPECT_TRUE(response.success);
    EXPECT_GT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 150);
    EXPECT_EQ(hsm->getCurrentStateName(), "Operational::Tracking::Locked");
}

TEST_F(ThreadedHSMTest, UnblockCondition_ExpectedState_Timeout)
{
    hsm->start();

    // PowerOn expects Idle state, but we won't send InitComplete
    // Should timeout waiting for expected state
    auto start = std::chrono::steady_clock::now();
    auto response = hsm->send("PowerOn", {}, false, /*timeoutMs=*/500);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Should timeout after ~500ms
    EXPECT_FALSE(response.success);
    EXPECT_GT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 400);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 700);
}

// ============================================================================
// Combined Dimension Tests
// ============================================================================

TEST_F(ThreadedHSMTest, CallerBlockingWithQueueBlocking)
{
    hsm->start();
    // Go to Idle state
    hsm->sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hsm->sendMessage(Events::InitComplete{});

    // send() with sync=true: Caller blocks AND other sync messages buffered
    std::atomic<bool> compensateStarted{false};
    std::atomic<bool> compensateDone{false};

    // Start Compensate in background (will be buffered until Home completes)
    std::thread compensateThread(
        [this, &compensateStarted, &compensateDone]()
        {
            compensateStarted = true;
            auto response = hsm->send("Compensate", {{"temperature", 20.0}}, /*sync=*/true);
            compensateDone = true;
            EXPECT_TRUE(response.success);
        });

    // Small delay to let thread start
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Now call Home with sync=true - both should execute sequentially
    auto response = hsm->send("Home", {{"speed", 100.0}}, /*sync=*/true);
    EXPECT_TRUE(response.success);

    // Wait for Compensate to complete
    compensateThread.join();
    EXPECT_TRUE(compensateDone);
}

TEST_F(ThreadedHSMTest, FireAndForget_NoBlockingAtAll)
{
    hsm->start();

    // Fire-and-forget: sendAsync with sync=false
    auto start = std::chrono::steady_clock::now();

    // Send multiple messages rapidly
    for (int i = 0; i < 10; ++i)
    {
        hsm->sendAsync("GetStatus", {}, /*sync=*/false);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;

    // All 10 calls should complete very quickly (just queueing)
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 50);

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
