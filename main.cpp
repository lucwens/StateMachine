/**
 * @file main.cpp
 * @brief Demonstration of Threaded HSM (Hierarchical State Machine)
 *        for a Laser Tracker using C++17 std::variant
 *
 * This program demonstrates:
 * 1. Hierarchical state nesting using std::variant
 * 2. Type-safe event/command handling with std::visit
 * 3. State entry/exit actions
 * 4. Composite states with sub-states
 * 5. Event-driven and command-driven state transitions
 * 6. Threaded HSM with Events and Commands
 * 7. Synchronous and asynchronous messaging
 * 8. JSON message protocol
 * 9. Galvanic separation between UI and worker thread
 *
 * Events: Past-tense notifications (InitComplete, TargetFound, etc.)
 * Commands: Imperative instructions (PowerOn, StartSearch, Home, etc.)
 *
 * Compile with: g++ -std=c++17 -pthread -o laser_tracker_hsm main.cpp
 */

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

void printSeparator(const std::string& title = "")
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
 * @brief Demo 1: Threaded HSM Basic Operation
 *
 * Demonstrates the threaded HSM with:
 * - HSM running in separate thread
 * - Async command and event sending
 * - Sync command and event sending
 */
void demoThreadedBasic()
{
    printSeparator("DEMO 1: Threaded HSM Basic Operation");

    ThreadedHSM tracker;
    tracker.start();

    std::cout << "\n--- Sending commands/events to HSM thread ---\n";

    // Send async command
    std::cout << "\nSending PowerOn command async...\n";
    tracker.sendMessageAsync(Commands::PowerOn{});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "Current state: " << tracker.getCurrentStateName() << "\n";

    // Send sync event and wait for response
    std::cout << "\nSending InitComplete event sync...\n";
    auto response = tracker.sendMessage(Events::InitComplete{});
    std::cout << "Response: success=" << (response.success ? "true" : "false") << ", state=" << tracker.getCurrentStateName() << "\n";

    // More async: command then event
    tracker.sendMessageAsync(Commands::StartSearch{});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    tracker.sendMessageAsync(Events::TargetFound{5000.0});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Final state: " << tracker.getCurrentStateName() << "\n";

    tracker.stop();
    std::cout << "\nDemo 1 completed successfully!\n";
}

/**
 * @brief Demo 2: Commands with State Restrictions
 *
 * Demonstrates commands that are restricted to specific states
 */
void demoCommands()
{
    printSeparator("DEMO 2: Commands with State Restrictions");

    ThreadedHSM tracker;
    tracker.start();

    // Setup: Power on and initialize
    tracker.sendMessage(Commands::PowerOn{});
    tracker.sendMessage(Events::InitComplete{});

    std::cout << "\n--- Testing Commands in Idle State ---\n";
    std::cout << "Current state: " << tracker.getCurrentStateName() << "\n";

    // Home command (sync, valid in Idle)
    std::cout << "\nSending Home command (sync)...\n";
    auto homeResult = tracker.sendMessage(Commands::Home{50.0});
    std::cout << "Home result: success=" << (homeResult.success ? "true" : "false") << "\n";
    if (homeResult.success)
    {
        std::cout << "Response JSON: " << homeResult.params.dump() << "\n";
    }

    // GetPosition command (async)
    std::cout << "\nSending GetPosition command...\n";
    auto posResult = tracker.sendMessage(Commands::GetPosition{});
    std::cout << "Position result: " << posResult.params.dump() << "\n";

    // SetLaserPower command
    std::cout << "\nSending SetLaserPower command...\n";
    Commands::SetLaserPower powerCmd;
    powerCmd.powerLevel = 0.75;
    auto powerResult    = tracker.sendMessage(powerCmd);
    std::cout << "Power result: success=" << (powerResult.success ? "true" : "false") << "\n";

    // Compensate command (sync)
    std::cout << "\nSending Compensate command (sync)...\n";
    Commands::Compensate compCmd;
    compCmd.temperature = 22.5;
    compCmd.pressure    = 1015.0;
    compCmd.humidity    = 45.0;
    auto compResult     = tracker.sendMessage(compCmd);
    std::cout << "Compensate result: " << compResult.params.dump() << "\n";

    // GetStatus command
    std::cout << "\nSending GetStatus command...\n";
    auto statusResult = tracker.sendMessage(Commands::GetStatus{});
    std::cout << "Status result: " << statusResult.params.dump() << "\n";

    // Try Home in wrong state
    std::cout << "\n--- Testing Home in wrong state ---\n";
    tracker.sendMessage(Commands::StartSearch{});
    tracker.sendMessage(Events::TargetFound{3000.0});
    std::cout << "Current state: " << tracker.getCurrentStateName() << "\n";

    auto invalidHome = tracker.sendMessage(Commands::Home{});
    std::cout << "Home in Locked state: success=" << (invalidHome.success ? "true" : "false") << ", error=" << invalidHome.error << "\n";

    tracker.stop();
    std::cout << "\nDemo 2 completed successfully!\n";
}

/**
 * @brief Demo 3: Synchronous Command Buffering
 *
 * Demonstrates that commands are buffered during sync command execution
 */
void demoCommandBuffering()
{
    printSeparator("DEMO 3: Synchronous Command Buffering");

    ThreadedHSM tracker;
    tracker.start();

    // Setup
    tracker.sendMessage(Commands::PowerOn{});
    tracker.sendMessage(Events::InitComplete{});
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    std::cout << "\n--- Sending multiple commands (sync Home will block others) ---\n";

    // Send a slow sync command (Home)
    std::cout << "Sending Home command (will take ~1 second)...\n";
    tracker.sendMessageAsync(Commands::Home{100.0});

    // Immediately send more commands - these should be buffered
    std::cout << "Sending GetPosition (should be buffered)...\n";
    tracker.sendMessageAsync(Commands::GetPosition{});

    std::cout << "Sending GetStatus (should be buffered)...\n";
    tracker.sendMessageAsync(Commands::GetStatus{});

    std::cout << "Sending SetLaserPower (should be buffered)...\n";
    tracker.sendMessageAsync(Commands::SetLaserPower{0.5});

    std::cout << "\nWaiting for all commands to complete...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    std::cout << "All buffered commands should have executed after Home completed.\n";

    tracker.stop();
    std::cout << "\nDemo 3 completed successfully!\n";
}

/**
 * @brief Demo 4: JSON Message Protocol
 *
 * Demonstrates sending raw JSON messages
 */
void demoJsonProtocol()
{
    printSeparator("DEMO 4: JSON Message Protocol");

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
    std::cout << "\nDemo 4 completed successfully!\n";
}

/**
 * @brief Demo 5: Multi-threaded Event Sending
 *
 * Demonstrates multiple threads sending events to the HSM
 */
void demoMultiThreaded()
{
    printSeparator("DEMO 5: Multi-threaded Event Sending");

    ThreadedHSM tracker;
    tracker.start();

    // Setup
    tracker.sendMessage(Commands::PowerOn{});
    tracker.sendMessage(Events::InitComplete{});
    tracker.sendMessage(Commands::StartSearch{});
    tracker.sendMessage(Events::TargetFound{2000.0});
    tracker.sendMessage(Commands::StartMeasure{});

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
                    tracker.sendMessageAsync(Events::MeasurementComplete{x, y, z});
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
    std::cout << "\nDemo 5 completed successfully!\n";
}

/**
 * @brief Demo 6: Complete Workflow
 *
 * Demonstrates a complete workflow through all states
 */
void demoCompleteWorkflow()
{
    printSeparator("DEMO 6: Complete Workflow");

    ThreadedHSM tracker;
    tracker.start();

    std::cout << "\n--- Running through complete laser tracker workflow ---\n";

    // Power on and initialize
    std::cout << "\n[1] Powering on...\n";
    auto resp = tracker.sendMessage(Commands::PowerOn{});
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    std::cout << "\n[2] Completing initialization...\n";
    resp = tracker.sendMessage(Events::InitComplete{});
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    // Home the system
    std::cout << "\n[3] Homing system...\n";
    auto homeResult = tracker.sendMessage(Commands::Home{75.0});
    std::cout << "Home result: " << (homeResult.success ? "success" : "failed") << "\n";

    // Start tracking
    std::cout << "\n[4] Starting target search...\n";
    resp = tracker.sendMessage(Commands::StartSearch{});
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    // Simulate finding target
    std::cout << "\n[5] Target found at 5000mm...\n";
    resp = tracker.sendMessage(Events::TargetFound{5000.0});
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    // Get position
    std::cout << "\n[6] Getting position...\n";
    auto posResult = tracker.sendMessage(Commands::GetPosition{});
    std::cout << "Position: " << posResult.params.dump() << "\n";

    // Start measuring
    std::cout << "\n[7] Starting measurement session...\n";
    resp = tracker.sendMessage(Commands::StartMeasure{});
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    // Record some measurements
    std::cout << "\n[8] Recording measurement points...\n";
    tracker.sendMessage(Events::MeasurementComplete{100.123, 200.456, 50.789});
    tracker.sendMessage(Events::MeasurementComplete{100.234, 200.567, 50.890});
    tracker.sendMessage(Events::MeasurementComplete{100.345, 200.678, 50.901});

    // Stop measuring
    std::cout << "\n[9] Stopping measurement...\n";
    resp = tracker.sendMessage(Commands::StopMeasure{});
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    // Return to idle
    std::cout << "\n[10] Returning to idle...\n";
    resp = tracker.sendMessage(Commands::ReturnToIdle{});
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    // Get final status
    std::cout << "\n[11] Final status check...\n";
    auto statusResult = tracker.sendMessage(Commands::GetStatus{});
    std::cout << "Status: " << statusResult.params.dump() << "\n";

    // Power off
    std::cout << "\n[12] Powering off...\n";
    resp = tracker.sendMessage(Commands::PowerOff{});
    std::cout << "State: " << tracker.getCurrentStateName() << "\n";

    tracker.stop();
    std::cout << "\nDemo 6 completed successfully!\n";
}

// ============================================================================
// Interactive Mode
// ============================================================================

void printHelp()
{
    std::cout << R"(
Available Messages:
  --- Commands (Imperative - "What to Do") ---
  power_on      - Turn on the laser tracker
  power_off     - Turn off the laser tracker
  search        - Start searching for target
  measure       - Start measuring
  stop          - Stop measuring
  idle          - Return to idle state
  reset         - Reset from error state
  home [speed]  - Move to home position (sync, Idle only)
  getpos        - Get current position (async)
  power <0-1>   - Set laser power level
  compensate <temp> <pressure> <humidity> - Apply compensation (sync)
  status        - Get system status
  move <az> <el> - Move relative (sync, Idle/Locked only)

  --- Events (Past Tense - "What Happened") ---
  init_ok       - Initialization completed
  init_fail     - Initialization failed
  found <dist>  - Target was found at distance (mm)
  lost          - Target was lost
  point <x> <y> <z> - Measurement point was recorded
  error <code>  - An error occurred

  --- Utilities ---
  state         - Print current state
  help          - Show this help
  quit          - Exit interactive mode
)";
}

/**
 * @brief Interactive Mode - uses ThreadedHSM with Events and Commands
 */
void runInteractiveMode()
{
    printSeparator("INTERACTIVE MODE (Threaded HSM with Events/Commands)");
    std::cout << "Control the Threaded Laser Tracker HSM interactively.\n";
    std::cout << "Messages run in a separate worker thread with sync/async support.\n";
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

        // Commands (imperative)
        if (cmd == "power_on")
        {
            auto resp = tracker.sendMessage(Commands::PowerOn{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "power_off")
        {
            auto resp = tracker.sendMessage(Commands::PowerOff{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "search")
        {
            auto resp = tracker.sendMessage(Commands::StartSearch{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "measure")
        {
            auto resp = tracker.sendMessage(Commands::StartMeasure{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "stop")
        {
            auto resp = tracker.sendMessage(Commands::StopMeasure{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "idle")
        {
            auto resp = tracker.sendMessage(Commands::ReturnToIdle{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "reset")
        {
            auto resp = tracker.sendMessage(Commands::Reset{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "home")
        {
            double speed = 100.0;
            iss >> speed;
            Commands::Home homeCmd;
            homeCmd.speed = speed;
            std::cout << "Executing Home command (sync, may take a moment)...\n";
            auto resp = tracker.sendMessage(homeCmd);
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
            auto resp = tracker.sendMessage(Commands::GetPosition{});
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
            auto resp           = tracker.sendMessage(powerCmd);
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
            auto resp = tracker.sendMessage(compCmd);
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
            auto resp = tracker.sendMessage(Commands::GetStatus{});
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
            auto resp = tracker.sendMessage(moveCmd);
            if (resp.success)
            {
                std::cout << "Move complete. Result: " << resp.params.dump() << "\n";
            }
            else
            {
                std::cout << "MoveRelative failed: " << resp.error << "\n";
            }
        }
        // Events (past tense)
        else if (cmd == "init_ok")
        {
            auto resp = tracker.sendMessage(Events::InitComplete{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "init_fail")
        {
            auto resp = tracker.sendMessage(Events::InitFailed{"User simulated failure"});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "found")
        {
            double dist = 1000.0;
            iss >> dist;
            auto resp = tracker.sendMessage(Events::TargetFound{dist});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "lost")
        {
            auto resp = tracker.sendMessage(Events::TargetLost{});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "point")
        {
            double x = 0, y = 0, z = 0;
            iss >> x >> y >> z;
            auto resp = tracker.sendMessage(Events::MeasurementComplete{x, y, z});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else if (cmd == "error")
        {
            int code = 99;
            iss >> code;
            auto resp = tracker.sendMessage(Events::ErrorOccurred{code, "User simulated error"});
            std::cout << "Result: " << (resp.success ? "handled" : "ignored") << "\n";
        }
        else
        {
            std::cout << "Unknown command: " << cmd << ". Type 'help' for available messages.\n";
        }
    }

    tracker.stop();
    std::cout << "\nExiting interactive mode.\n";
}

// ============================================================================
// Main Entry Point
// ============================================================================

void printMainMenu()
{
    std::cout << R"(
================================================================
   Laser Tracker Threaded HSM Demo - C++17 std::variant
================================================================

This program demonstrates the Hierarchical State Machine (HSM)
pattern using std::variant for type-safe state representation.

The HSM runs in a dedicated worker thread, providing galvanic
separation between the main/UI thread and the state machine engine.

=== Demos ===
  1. Threaded HSM Basic Operation
  2. Commands with State Restrictions
  3. Synchronous Command Buffering
  4. JSON Message Protocol
  5. Multi-threaded Event Sending
  6. Complete Workflow

=== Interactive Mode ===
  7. Interactive Mode (with full command support)

=== Batch Operations ===
  8. Run All Demos

  0. Exit

)";
}

int main(int argc, char* argv[])
{
    // Check for command-line arguments
    if (argc > 1)
    {
        std::string arg = argv[1];
        if (arg == "--all" || arg == "-a")
        {
            demoThreadedBasic();
            demoCommands();
            demoCommandBuffering();
            demoJsonProtocol();
            demoMultiThreaded();
            demoCompleteWorkflow();
            printSeparator("ALL DEMOS COMPLETED SUCCESSFULLY");
            return 0;
        }
        else if (arg == "--interactive" || arg == "-i")
        {
            runInteractiveMode();
            return 0;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --all, -a          Run all demos\n"
                      << "  --interactive, -i  Run interactive mode\n"
                      << "  --help, -h         Show this help\n"
                      << "  (no args)          Show menu\n";
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
            case 1:
                demoThreadedBasic();
                break;
            case 2:
                demoCommands();
                break;
            case 3:
                demoCommandBuffering();
                break;
            case 4:
                demoJsonProtocol();
                break;
            case 5:
                demoMultiThreaded();
                break;
            case 6:
                demoCompleteWorkflow();
                break;
            case 7:
                runInteractiveMode();
                break;
            case 8:
                demoThreadedBasic();
                demoCommands();
                demoCommandBuffering();
                demoJsonProtocol();
                demoMultiThreaded();
                demoCompleteWorkflow();
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
