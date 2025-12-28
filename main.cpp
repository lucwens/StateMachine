/**
 * @file main.cpp
 * @brief Demonstration of HSM (Hierarchical State Machine) State Pattern
 *        for a Laser Tracker using C++17 std::variant
 *
 * This program demonstrates:
 * 1. Hierarchical state nesting using std::variant
 * 2. Type-safe event handling with std::visit
 * 3. State entry/exit actions
 * 4. Composite states with sub-states
 * 5. Event-driven state transitions
 * 6. Threaded HSM with command support
 * 7. Synchronous and asynchronous commands
 * 8. JSON message protocol
 *
 * Compile with: g++ -std=c++17 -pthread -o laser_tracker_hsm main.cpp
 */

#include "LaserTrackerHSM.hpp"
#include "ThreadedHSM.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace LaserTracker;

// ============================================================================
// Helper function to print separator
// ============================================================================

void printSeparator(const std::string &title = "")
{
    std::cout << "\n";
    std::cout << "================================================================\n";
    if (!title.empty())
    {
        std::cout << "  " << title << "\n";
        std::cout << "================================================================\n";
    }
}

// ============================================================================
// Demo Scenarios
// ============================================================================

/**
 * @brief Demo 1: Normal Operation Flow
 *
 * Demonstrates the typical happy-path workflow:
 * Off -> Operational::Initializing -> Idle -> Tracking::Searching
 *     -> Tracking::Locked -> Tracking::Measuring -> back to Idle -> Off
 */
void demoNormalOperation()
{
    printSeparator("DEMO 1: Normal Operation Flow");

    HSM tracker;
    tracker.printState();

    // Power on the tracker
    tracker.processEvent(Events::PowerOn{});
    tracker.printState();

    // Initialization completes successfully
    tracker.processEvent(Events::InitComplete{});
    tracker.printState();

    // Start searching for target
    tracker.processEvent(Events::StartSearch{});
    tracker.printState();

    // Target found at 5000mm (5 meters)
    tracker.processEvent(Events::TargetFound{5000.0});
    tracker.printState();

    // Start measuring
    tracker.processEvent(Events::StartMeasure{});
    tracker.printState();

    // Record some measurement points
    tracker.processEvent(Events::MeasurementComplete{100.123456, 200.654321, 50.111111});
    tracker.processEvent(Events::MeasurementComplete{100.234567, 200.765432, 50.222222});
    tracker.processEvent(Events::MeasurementComplete{100.345678, 200.876543, 50.333333});

    // Stop measuring
    tracker.processEvent(Events::StopMeasure{});
    tracker.printState();

    // Return to idle
    tracker.processEvent(Events::ReturnToIdle{});
    tracker.printState();

    // Power off
    tracker.processEvent(Events::PowerOff{});
    tracker.printState();

    std::cout << "\nDemo 1 completed successfully!\n";
}

/**
 * @brief Demo 2: Error Handling and Recovery
 *
 * Demonstrates error handling:
 * - Initialization failure
 * - Error during tracking
 * - Reset and recovery
 */
void demoErrorHandling()
{
    printSeparator("DEMO 2: Error Handling and Recovery");

    HSM tracker;

    // Power on
    tracker.processEvent(Events::PowerOn{});
    tracker.printState();

    // Simulate initialization failure
    tracker.processEvent(Events::InitFailed{"Motor calibration failed"});
    tracker.printState();

    // Reset the system
    tracker.processEvent(Events::Reset{});
    tracker.printState();

    // This time initialization succeeds
    tracker.processEvent(Events::InitComplete{});
    tracker.printState();

    // Start tracking
    tracker.processEvent(Events::StartSearch{});
    tracker.processEvent(Events::TargetFound{3500.0});
    tracker.printState();

    // Error occurs during tracking
    tracker.processEvent(Events::ErrorOccurred{42, "Beam interrupted"});
    tracker.printState();

    // Reset again
    tracker.processEvent(Events::Reset{});
    tracker.processEvent(Events::InitComplete{});
    tracker.printState();

    std::cout << "\nDemo 2 completed successfully!\n";
}

/**
 * @brief Demo 3: Target Loss and Reacquisition
 *
 * Demonstrates target tracking behavior:
 * - Target loss during locked state
 * - Target loss during measuring
 * - Reacquisition workflow
 */
void demoTargetLoss()
{
    printSeparator("DEMO 3: Target Loss and Reacquisition");

    HSM tracker;

    // Quick setup to tracking state
    tracker.processEvent(Events::PowerOn{});
    tracker.processEvent(Events::InitComplete{});
    tracker.processEvent(Events::StartSearch{});
    tracker.processEvent(Events::TargetFound{2000.0});
    tracker.printState();

    // Lose target while locked
    tracker.processEvent(Events::TargetLost{});
    tracker.printState();

    // Find target again
    tracker.processEvent(Events::TargetFound{2100.0});
    tracker.printState();

    // Start measuring
    tracker.processEvent(Events::StartMeasure{});
    tracker.processEvent(Events::MeasurementComplete{50.0, 75.0, 25.0});
    tracker.printState();

    // Lose target during measurement
    tracker.processEvent(Events::TargetLost{});
    tracker.printState();

    // Reacquire target
    tracker.processEvent(Events::TargetFound{2150.0});
    tracker.printState();

    std::cout << "\nDemo 3 completed successfully!\n";
}

/**
 * @brief Demo 4: Invalid Event Handling
 *
 * Demonstrates that invalid events are properly ignored:
 * - Cannot search while off
 * - Cannot measure while searching
 * - Events are rejected with feedback
 */
void demoInvalidEvents()
{
    printSeparator("DEMO 4: Invalid Event Handling");

    HSM tracker;
    tracker.printState();

    // Try to start search while off (should be ignored)
    std::cout << "\n-- Attempting invalid events in Off state --\n";
    tracker.processEvent(Events::StartSearch{});

    // Try InitComplete while off (should be ignored)
    tracker.processEvent(Events::InitComplete{});

    // Now power on and go to idle
    tracker.processEvent(Events::PowerOn{});
    tracker.processEvent(Events::InitComplete{});
    tracker.printState();

    // Try to measure while in idle (should be ignored)
    std::cout << "\n-- Attempting invalid events in Idle state --\n";
    tracker.processEvent(Events::StartMeasure{});

    // Try target lost while in idle (should be ignored)
    tracker.processEvent(Events::TargetLost{});

    // Go to searching
    tracker.processEvent(Events::StartSearch{});
    tracker.printState();

    // Try to start measure while searching (should be ignored)
    std::cout << "\n-- Attempting invalid events in Searching state --\n";
    tracker.processEvent(Events::StartMeasure{});

    std::cout << "\nDemo 4 completed successfully!\n";
}

/**
 * @brief Demo 5: State Inspection API
 *
 * Demonstrates the state query capabilities:
 * - Checking current state type
 * - Getting hierarchical state names
 * - State pattern introspection
 */
void demoStateInspection()
{
    printSeparator("DEMO 5: State Inspection API");

    HSM tracker;

    // Check initial state
    std::cout << "Is in Off state? " << (tracker.isInState<States::Off>() ? "Yes" : "No") << "\n";
    std::cout << "Is in Operational state? " << (tracker.isInState<States::Operational>() ? "Yes" : "No") << "\n";
    std::cout << "Full state path: " << tracker.getCurrentStateName() << "\n\n";

    // Power on
    tracker.processEvent(Events::PowerOn{});
    std::cout << "Is in Off state? " << (tracker.isInState<States::Off>() ? "Yes" : "No") << "\n";
    std::cout << "Is in Operational state? " << (tracker.isInState<States::Operational>() ? "Yes" : "No") << "\n";
    std::cout << "Full state path: " << tracker.getCurrentStateName() << "\n\n";

    // Complete initialization and go to tracking
    tracker.processEvent(Events::InitComplete{});
    std::cout << "Full state path: " << tracker.getCurrentStateName() << "\n";

    tracker.processEvent(Events::StartSearch{});
    std::cout << "Full state path: " << tracker.getCurrentStateName() << "\n";

    tracker.processEvent(Events::TargetFound{1000.0});
    std::cout << "Full state path: " << tracker.getCurrentStateName() << "\n";

    tracker.processEvent(Events::StartMeasure{});
    std::cout << "Full state path: " << tracker.getCurrentStateName() << "\n";

    std::cout << "\nDemo 5 completed successfully!\n";
}

/**
 * @brief Demo 6: Comprehensive Stress Test
 *
 * Runs through many state transitions to verify HSM robustness
 */
void demoStressTest()
{
    printSeparator("DEMO 6: Comprehensive State Transition Test");

    HSM tracker;
    int successfulTransitions = 0;
    int ignoredEvents         = 0;

    auto processAndCount      = [&](const Event &e)
    {
        if (tracker.processEvent(e))
        {
            ++successfulTransitions;
        }
        else
        {
            ++ignoredEvents;
        }
    };

    // Run through multiple complete cycles
    for (int cycle = 1; cycle <= 3; ++cycle)
    {
        std::cout << "\n--- Cycle " << cycle << " ---\n";

        // Full workflow
        processAndCount(Events::PowerOn{});
        processAndCount(Events::InitComplete{});
        processAndCount(Events::StartSearch{});
        processAndCount(Events::TargetFound{1000.0 * cycle});
        processAndCount(Events::StartMeasure{});

        // Multiple measurements
        for (int m = 0; m < 5; ++m)
        {
            processAndCount(Events::MeasurementComplete{static_cast<double>(m), static_cast<double>(m * 2), static_cast<double>(m * 3)});
        }

        processAndCount(Events::StopMeasure{});
        processAndCount(Events::ReturnToIdle{});
        processAndCount(Events::PowerOff{});
    }

    std::cout << "\n--- Test Summary ---\n";
    std::cout << "Successful transitions: " << successfulTransitions << "\n";
    std::cout << "Ignored events: " << ignoredEvents << "\n";
    std::cout << "Final state: " << tracker.getCurrentStateName() << "\n";

    std::cout << "\nDemo 6 completed successfully!\n";
}

// ============================================================================
// Threaded HSM Demos
// ============================================================================

/**
 * @brief Demo 7: Threaded HSM Basic Operation
 *
 * Demonstrates the threaded HSM with:
 * - HSM running in separate thread
 * - Async event sending
 * - Sync event sending
 */
void demoThreadedBasic()
{
    printSeparator("DEMO 7: Threaded HSM Basic Operation");

    ThreadedHSM tracker;
    tracker.start();

    std::cout << "\n--- Sending events to HSM thread ---\n";

    // Send async events
    std::cout << "\nSending PowerOn async...\n";
    tracker.sendEventAsync(Events::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "Current state: " << tracker.getCurrentStateName() << "\n";

    // Send sync event and wait for response
    std::cout << "\nSending InitComplete sync...\n";
    auto response = tracker.sendEventSync(Events::InitComplete{});
    std::cout << "Response: success=" << (response.success ? "true" : "false") << ", state=" << tracker.getCurrentStateName() << "\n";

    // More async events
    tracker.sendEventAsync(Events::StartSearch{});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    tracker.sendEventAsync(Events::TargetFound{5000.0});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Final state: " << tracker.getCurrentStateName() << "\n";

    tracker.stop();
    std::cout << "\nDemo 7 completed successfully!\n";
}

/**
 * @brief Demo 8: Commands with State Restrictions
 *
 * Demonstrates commands that are restricted to specific states
 */
void demoCommands()
{
    printSeparator("DEMO 8: Commands with State Restrictions");

    ThreadedHSM tracker;
    tracker.start();

    // Setup: Power on and initialize
    tracker.sendEventSync(Events::PowerOn{});
    tracker.sendEventSync(Events::InitComplete{});

    std::cout << "\n--- Testing Commands in Idle State ---\n";
    std::cout << "Current state: " << tracker.getCurrentStateName() << "\n";

    // Home command (sync, valid in Idle)
    std::cout << "\nSending Home command (sync)...\n";
    auto homeResult = tracker.sendCommand(Commands::Home{50.0});
    std::cout << "Home result: success=" << (homeResult.success ? "true" : "false") << "\n";
    if (homeResult.success)
    {
        std::cout << "Response JSON: " << homeResult.params.dump() << "\n";
    }

    // GetPosition command (async)
    std::cout << "\nSending GetPosition command...\n";
    auto posResult = tracker.sendCommand(Commands::GetPosition{});
    std::cout << "Position result: " << posResult.params.dump() << "\n";

    // SetLaserPower command
    std::cout << "\nSending SetLaserPower command...\n";
    Commands::SetLaserPower powerCmd;
    powerCmd.powerLevel = 0.75;
    auto powerResult    = tracker.sendCommand(powerCmd);
    std::cout << "Power result: success=" << (powerResult.success ? "true" : "false") << "\n";

    // Compensate command (sync)
    std::cout << "\nSending Compensate command (sync)...\n";
    Commands::Compensate compCmd;
    compCmd.temperature = 22.5;
    compCmd.pressure    = 1015.0;
    compCmd.humidity    = 45.0;
    auto compResult     = tracker.sendCommand(compCmd);
    std::cout << "Compensate result: " << compResult.params.dump() << "\n";

    // GetStatus command
    std::cout << "\nSending GetStatus command...\n";
    auto statusResult = tracker.sendCommand(Commands::GetStatus{});
    std::cout << "Status result: " << statusResult.params.dump() << "\n";

    // Try Home in wrong state
    std::cout << "\n--- Testing Home in wrong state ---\n";
    tracker.sendEventSync(Events::StartSearch{});
    tracker.sendEventSync(Events::TargetFound{3000.0});
    std::cout << "Current state: " << tracker.getCurrentStateName() << "\n";

    auto invalidHome = tracker.sendCommand(Commands::Home{});
    std::cout << "Home in Locked state: success=" << (invalidHome.success ? "true" : "false") << ", error=" << invalidHome.error << "\n";

    tracker.stop();
    std::cout << "\nDemo 8 completed successfully!\n";
}

/**
 * @brief Demo 9: Synchronous Command Buffering
 *
 * Demonstrates that commands are buffered during sync command execution
 */
void demoCommandBuffering()
{
    printSeparator("DEMO 9: Synchronous Command Buffering");

    ThreadedHSM tracker;
    tracker.start();

    // Setup
    tracker.sendEventSync(Events::PowerOn{});
    tracker.sendEventSync(Events::InitComplete{});
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    std::cout << "\n--- Sending multiple commands (sync Home will block others) ---\n";

    // Send a slow sync command (Home)
    std::cout << "Sending Home command (will take ~1 second)...\n";
    tracker.sendCommandAsync(Commands::Home{100.0});

    // Immediately send more commands - these should be buffered
    std::cout << "Sending GetPosition (should be buffered)...\n";
    tracker.sendCommandAsync(Commands::GetPosition{});

    std::cout << "Sending GetStatus (should be buffered)...\n";
    tracker.sendCommandAsync(Commands::GetStatus{});

    std::cout << "Sending SetLaserPower (should be buffered)...\n";
    tracker.sendCommandAsync(Commands::SetLaserPower{0.5});

    std::cout << "\nWaiting for all commands to complete...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    std::cout << "All buffered commands should have executed after Home completed.\n";

    tracker.stop();
    std::cout << "\nDemo 9 completed successfully!\n";
}

/**
 * @brief Demo 10: JSON Message Protocol
 *
 * Demonstrates sending raw JSON messages
 */
void demoJsonProtocol()
{
    printSeparator("DEMO 10: JSON Message Protocol");

    ThreadedHSM tracker;
    tracker.start();

    std::cout << "\n--- Sending JSON messages ---\n";

    // Send events via JSON
    std::cout << "\nSending PowerOn via JSON:\n";
    std::string json1 = R"({"id": 100, "type": "event", "name": "PowerOn", "sync": true, "needsReply": true})";
    std::cout << "  " << json1 << "\n";
    tracker.sendJsonMessage(json1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    std::cout << "\nSending InitComplete via JSON:\n";
    std::string json2 = R"({"id": 101, "type": "event", "name": "InitComplete", "sync": false})";
    std::cout << "  " << json2 << "\n";
    tracker.sendJsonMessage(json2);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    // Send command via JSON
    std::cout << "\nSending GetStatus command via JSON:\n";
    std::string json3 = R"({"id": 200, "type": "command", "name": "GetStatus", "sync": false, "needsReply": true})";
    std::cout << "  " << json3 << "\n";
    tracker.sendJsonMessage(json3);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Check for responses
    auto resp = tracker.tryGetResponse();
    if (resp)
    {
        std::cout << "\nReceived response: " << resp->toJson() << "\n";
    }

    tracker.stop();
    std::cout << "\nDemo 10 completed successfully!\n";
}

/**
 * @brief Demo 11: Multi-threaded Event Sending
 *
 * Demonstrates multiple threads sending events to the HSM
 */
void demoMultiThreaded()
{
    printSeparator("DEMO 11: Multi-threaded Event Sending");

    ThreadedHSM tracker;
    tracker.start();

    // Setup
    tracker.sendEventSync(Events::PowerOn{});
    tracker.sendEventSync(Events::InitComplete{});
    tracker.sendEventSync(Events::StartSearch{});
    tracker.sendEventSync(Events::TargetFound{2000.0});
    tracker.sendEventSync(Events::StartMeasure{});

    std::cout << "\n--- Multiple threads sending measurement events ---\n";
    std::cout << "Initial state: " << tracker.getCurrentStateName() << "\n";

    std::atomic<int> eventCount{0};

    // Launch multiple threads sending events
    std::vector<std::thread> threads;
    for (int t = 0; t < 3; ++t)
    {
        threads.emplace_back(
            [&tracker, &eventCount, t]()
            {
                for (int i = 0; i < 5; ++i)
                {
                    double x = t * 100.0 + i;
                    double y = t * 200.0 + i;
                    double z = t * 50.0 + i;
                    tracker.sendEventAsync(Events::MeasurementComplete{x, y, z});
                    eventCount++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            });
    }

    // Wait for all threads
    for (auto& t : threads)
    {
        t.join();
    }

    std::cout << "\nTotal events sent from " << threads.size() << " threads: " << eventCount.load() << "\n";
    std::cout << "Final state: " << tracker.getCurrentStateName() << "\n";

    tracker.stop();
    std::cout << "\nDemo 11 completed successfully!\n";
}

// ============================================================================
// Interactive Mode
// ============================================================================

void printHelp()
{
    std::cout << R"(
Available Commands:
  --- Events (trigger state changes) ---
  power_on      - Power on the laser tracker
  power_off     - Power off the laser tracker
  init_ok       - Signal initialization complete
  init_fail     - Signal initialization failed
  search        - Start searching for target
  found <dist>  - Target found at distance (mm)
  lost          - Target lost
  measure       - Start measuring
  point <x> <y> <z> - Record a measurement point
  stop          - Stop measuring
  idle          - Return to idle state
  error <code>  - Simulate an error
  reset         - Reset from error state

  --- Commands (no state change, may return results) ---
  home [speed]  - Move to home position (sync, Idle only)
  getpos        - Get current position (async)
  power <0-1>   - Set laser power level
  compensate <temp> <pressure> <humidity> - Apply compensation (sync)
  status        - Get system status
  move <az> <el> - Move relative (sync, Idle/Locked only)

  --- Utilities ---
  state         - Print current state
  help          - Show this help
  quit          - Exit interactive mode
)";
}

void runInteractiveMode()
{
    printSeparator("INTERACTIVE MODE (Basic HSM)");
    std::cout << "Control the Laser Tracker HSM interactively.\n";
    std::cout << "Note: This uses the basic (non-threaded) HSM. Use mode 9 for threaded.\n";
    printHelp();

    HSM         tracker;
    std::string line;

    while (true)
    {
        std::cout << "\n[" << tracker.getCurrentStateName() << "] > ";
        if (!std::getline(std::cin, line))
            break;

        std::istringstream iss(line);
        std::string        cmd;
        iss >> cmd;

        if (cmd.empty())
            continue;
        if (cmd == "quit" || cmd == "exit")
            break;
        if (cmd == "help")
        {
            printHelp();
            continue;
        }
        if (cmd == "state")
        {
            tracker.printState();
            continue;
        }

        if (cmd == "power_on")
        {
            tracker.processEvent(Events::PowerOn{});
        }
        else if (cmd == "power_off")
        {
            tracker.processEvent(Events::PowerOff{});
        }
        else if (cmd == "init_ok")
        {
            tracker.processEvent(Events::InitComplete{});
        }
        else if (cmd == "init_fail")
        {
            tracker.processEvent(Events::InitFailed{"User simulated failure"});
        }
        else if (cmd == "search")
        {
            tracker.processEvent(Events::StartSearch{});
        }
        else if (cmd == "found")
        {
            double dist = 1000.0;
            iss >> dist;
            tracker.processEvent(Events::TargetFound{dist});
        }
        else if (cmd == "lost")
        {
            tracker.processEvent(Events::TargetLost{});
        }
        else if (cmd == "measure")
        {
            tracker.processEvent(Events::StartMeasure{});
        }
        else if (cmd == "point")
        {
            double x = 0, y = 0, z = 0;
            iss >> x >> y >> z;
            tracker.processEvent(Events::MeasurementComplete{x, y, z});
        }
        else if (cmd == "stop")
        {
            tracker.processEvent(Events::StopMeasure{});
        }
        else if (cmd == "idle")
        {
            tracker.processEvent(Events::ReturnToIdle{});
        }
        else if (cmd == "error")
        {
            int code = 99;
            iss >> code;
            tracker.processEvent(Events::ErrorOccurred{code, "User simulated error"});
        }
        else if (cmd == "reset")
        {
            tracker.processEvent(Events::Reset{});
        }
        else
        {
            std::cout << "Unknown command: " << cmd << ". Type 'help' for available commands.\n";
        }
    }

    std::cout << "\nExiting interactive mode.\n";
}

/**
 * @brief Threaded Interactive Mode - uses ThreadedHSM with full command support
 */
void runThreadedInteractiveMode()
{
    printSeparator("INTERACTIVE MODE (Threaded HSM with Commands)");
    std::cout << "Control the Threaded Laser Tracker HSM interactively.\n";
    std::cout << "Commands run in a separate thread with sync/async support.\n";
    printHelp();

    ThreadedHSM tracker;
    tracker.start();

    std::string line;

    while (true)
    {
        std::cout << "\n[" << tracker.getCurrentStateName() << "] > ";
        if (!std::getline(std::cin, line))
            break;

        std::istringstream iss(line);
        std::string        cmd;
        iss >> cmd;

        if (cmd.empty())
            continue;
        if (cmd == "quit" || cmd == "exit")
            break;
        if (cmd == "help")
        {
            printHelp();
            continue;
        }
        if (cmd == "state")
        {
            std::cout << "Current State: [" << tracker.getCurrentStateName() << "]\n";
            continue;
        }

        // Events
        if (cmd == "power_on")
        {
            auto resp = tracker.sendEventSync(Events::PowerOn{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "power_off")
        {
            auto resp = tracker.sendEventSync(Events::PowerOff{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "init_ok")
        {
            auto resp = tracker.sendEventSync(Events::InitComplete{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "init_fail")
        {
            auto resp = tracker.sendEventSync(Events::InitFailed{"User simulated failure"});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "search")
        {
            auto resp = tracker.sendEventSync(Events::StartSearch{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "found")
        {
            double dist = 1000.0;
            iss >> dist;
            auto resp = tracker.sendEventSync(Events::TargetFound{dist});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "lost")
        {
            auto resp = tracker.sendEventSync(Events::TargetLost{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "measure")
        {
            auto resp = tracker.sendEventSync(Events::StartMeasure{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "point")
        {
            double x = 0, y = 0, z = 0;
            iss >> x >> y >> z;
            auto resp = tracker.sendEventSync(Events::MeasurementComplete{x, y, z});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "stop")
        {
            auto resp = tracker.sendEventSync(Events::StopMeasure{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "idle")
        {
            auto resp = tracker.sendEventSync(Events::ReturnToIdle{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "error")
        {
            int code = 99;
            iss >> code;
            auto resp = tracker.sendEventSync(Events::ErrorOccurred{code, "User simulated error"});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "reset")
        {
            auto resp = tracker.sendEventSync(Events::Reset{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        // Commands
        else if (cmd == "home")
        {
            double speed = 100.0;
            iss >> speed;
            Commands::Home homeCmd;
            homeCmd.speed = speed;
            std::cout << "Executing Home command (sync, may take a moment)...\n";
            auto resp = tracker.sendCommand(homeCmd);
            if (resp.success)
            {
                std::cout << "Home complete. Result: " << resp.params.dump() << "\n";
            }
            else
            {
                std::cout << "Home failed: " << resp.error << "\n";
            }
        }
        else if (cmd == "getpos")
        {
            auto resp = tracker.sendCommand(Commands::GetPosition{});
            if (resp.success)
            {
                std::cout << "Position: " << resp.params.dump() << "\n";
            }
            else
            {
                std::cout << "GetPosition failed: " << resp.error << "\n";
            }
        }
        else if (cmd == "power")
        {
            double level = 1.0;
            iss >> level;
            Commands::SetLaserPower powerCmd;
            powerCmd.powerLevel = level;
            auto resp           = tracker.sendCommand(powerCmd);
            if (resp.success)
            {
                std::cout << "Power set to " << (level * 100) << "%\n";
            }
            else
            {
                std::cout << "SetLaserPower failed: " << resp.error << "\n";
            }
        }
        else if (cmd == "compensate")
        {
            double temp = 20.0, pressure = 1013.25, humidity = 50.0;
            iss >> temp >> pressure >> humidity;
            Commands::Compensate compCmd;
            compCmd.temperature = temp;
            compCmd.pressure    = pressure;
            compCmd.humidity    = humidity;
            std::cout << "Executing Compensate command (sync)...\n";
            auto resp = tracker.sendCommand(compCmd);
            if (resp.success)
            {
                std::cout << "Compensation applied. Result: " << resp.params.dump() << "\n";
            }
            else
            {
                std::cout << "Compensate failed: " << resp.error << "\n";
            }
        }
        else if (cmd == "status")
        {
            auto resp = tracker.sendCommand(Commands::GetStatus{});
            if (resp.success)
            {
                std::cout << "Status: " << resp.params.dump() << "\n";
            }
            else
            {
                std::cout << "GetStatus failed: " << resp.error << "\n";
            }
        }
        else if (cmd == "move")
        {
            double az = 0.0, el = 0.0;
            iss >> az >> el;
            Commands::MoveRelative moveCmd;
            moveCmd.azimuth   = az;
            moveCmd.elevation = el;
            std::cout << "Executing MoveRelative command (sync)...\n";
            auto resp = tracker.sendCommand(moveCmd);
            if (resp.success)
            {
                std::cout << "Move complete. Result: " << resp.params.dump() << "\n";
            }
            else
            {
                std::cout << "MoveRelative failed: " << resp.error << "\n";
            }
        }
        else
        {
            std::cout << "Unknown command: " << cmd << ". Type 'help' for available commands.\n";
        }
    }

    tracker.stop();
    std::cout << "\nExiting threaded interactive mode.\n";
}

// ============================================================================
// Main Entry Point
// ============================================================================

void printMainMenu()
{
    std::cout << R"(
================================================================
   Laser Tracker HSM Demo - C++17 std::variant Implementation
================================================================

This program demonstrates the Hierarchical State Machine (HSM)
pattern using std::variant for type-safe state representation.

=== Basic HSM Demos (Single-threaded) ===
  1. Normal Operation Flow
  2. Error Handling and Recovery
  3. Target Loss and Reacquisition
  4. Invalid Event Handling
  5. State Inspection API
  6. Comprehensive Stress Test

=== Threaded HSM Demos (Multi-threaded with Commands) ===
  7. Threaded HSM Basic Operation
  8. Commands with State Restrictions
  9. Synchronous Command Buffering
  10. JSON Message Protocol
  11. Multi-threaded Event Sending

=== Interactive Modes ===
  12. Basic Interactive Mode (single-threaded)
  13. Threaded Interactive Mode (with commands)

=== Batch Operations ===
  14. Run All Basic Demos (1-6)
  15. Run All Threaded Demos (7-11)
  16. Run All Demos

  0. Exit

)";
}

int main(int argc, char *argv[])
{
    // Check for command-line arguments
    if (argc > 1)
    {
        std::string arg = argv[1];
        if (arg == "--all" || arg == "-a")
        {
            // Run all basic demos
            demoNormalOperation();
            demoErrorHandling();
            demoTargetLoss();
            demoInvalidEvents();
            demoStateInspection();
            demoStressTest();
            // Run all threaded demos
            demoThreadedBasic();
            demoCommands();
            demoCommandBuffering();
            demoJsonProtocol();
            demoMultiThreaded();
            printSeparator("ALL DEMOS COMPLETED SUCCESSFULLY");
            return 0;
        }
        else if (arg == "--basic" || arg == "-b")
        {
            demoNormalOperation();
            demoErrorHandling();
            demoTargetLoss();
            demoInvalidEvents();
            demoStateInspection();
            demoStressTest();
            printSeparator("ALL BASIC DEMOS COMPLETED SUCCESSFULLY");
            return 0;
        }
        else if (arg == "--threaded" || arg == "-t")
        {
            demoThreadedBasic();
            demoCommands();
            demoCommandBuffering();
            demoJsonProtocol();
            demoMultiThreaded();
            printSeparator("ALL THREADED DEMOS COMPLETED SUCCESSFULLY");
            return 0;
        }
        else if (arg == "--interactive" || arg == "-i")
        {
            runInteractiveMode();
            return 0;
        }
        else if (arg == "--threaded-interactive" || arg == "-ti")
        {
            runThreadedInteractiveMode();
            return 0;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --all, -a                  Run all demos\n"
                      << "  --basic, -b                Run basic (non-threaded) demos\n"
                      << "  --threaded, -t             Run threaded demos\n"
                      << "  --interactive, -i          Run basic interactive mode\n"
                      << "  --threaded-interactive, -ti Run threaded interactive mode\n"
                      << "  --help, -h                 Show this help\n"
                      << "  (no args)                  Show menu\n";
            return 0;
        }
    }

    // Interactive menu
    while (true)
    {
        printMainMenu();
        std::cout << "Enter choice: ";

        int choice;
        if (!(std::cin >> choice))
        {
            std::cin.clear();
            std::cin.ignore(10000, '\n');
            continue;
        }
        std::cin.ignore(10000, '\n');

        switch (choice)
        {
            case 0:
                std::cout << "Goodbye!\n";
                return 0;
            // Basic demos
            case 1:
                demoNormalOperation();
                break;
            case 2:
                demoErrorHandling();
                break;
            case 3:
                demoTargetLoss();
                break;
            case 4:
                demoInvalidEvents();
                break;
            case 5:
                demoStateInspection();
                break;
            case 6:
                demoStressTest();
                break;
            // Threaded demos
            case 7:
                demoThreadedBasic();
                break;
            case 8:
                demoCommands();
                break;
            case 9:
                demoCommandBuffering();
                break;
            case 10:
                demoJsonProtocol();
                break;
            case 11:
                demoMultiThreaded();
                break;
            // Interactive modes
            case 12:
                runInteractiveMode();
                break;
            case 13:
                runThreadedInteractiveMode();
                break;
            // Batch operations
            case 14:
                demoNormalOperation();
                demoErrorHandling();
                demoTargetLoss();
                demoInvalidEvents();
                demoStateInspection();
                demoStressTest();
                printSeparator("ALL BASIC DEMOS COMPLETED SUCCESSFULLY");
                break;
            case 15:
                demoThreadedBasic();
                demoCommands();
                demoCommandBuffering();
                demoJsonProtocol();
                demoMultiThreaded();
                printSeparator("ALL THREADED DEMOS COMPLETED SUCCESSFULLY");
                break;
            case 16:
                demoNormalOperation();
                demoErrorHandling();
                demoTargetLoss();
                demoInvalidEvents();
                demoStateInspection();
                demoStressTest();
                demoThreadedBasic();
                demoCommands();
                demoCommandBuffering();
                demoJsonProtocol();
                demoMultiThreaded();
                printSeparator("ALL DEMOS COMPLETED SUCCESSFULLY");
                break;
            default:
                std::cout << "Invalid choice. Please try again.\n";
        }

        std::cout << "\nPress Enter to continue...";
        std::cin.get();
    }

    return 0;
}
