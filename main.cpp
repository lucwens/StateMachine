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
 *
 * Compile with: g++ -std=c++17 -o laser_tracker_hsm main.cpp
 */

#include "LaserTrackerHSM.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

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
// Interactive Mode
// ============================================================================

void printHelp()
{
    std::cout << R"(
Available Commands:
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
  state         - Print current state
  help          - Show this help
  quit          - Exit interactive mode
)";
}

void runInteractiveMode()
{
    printSeparator("INTERACTIVE MODE");
    std::cout << "Control the Laser Tracker HSM interactively.\n";
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

Select a demo to run:
  1. Normal Operation Flow
  2. Error Handling and Recovery
  3. Target Loss and Reacquisition
  4. Invalid Event Handling
  5. State Inspection API
  6. Comprehensive Stress Test
  7. Run All Demos
  8. Interactive Mode
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
            demoNormalOperation();
            demoErrorHandling();
            demoTargetLoss();
            demoInvalidEvents();
            demoStateInspection();
            demoStressTest();
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
                      << "  --all, -a         Run all demos\n"
                      << "  --interactive, -i Run interactive mode\n"
                      << "  --help, -h        Show this help\n"
                      << "  (no args)         Show menu\n";
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
            case 7:
                demoNormalOperation();
                demoErrorHandling();
                demoTargetLoss();
                demoInvalidEvents();
                demoStateInspection();
                demoStressTest();
                printSeparator("ALL DEMOS COMPLETED SUCCESSFULLY");
                break;
            case 8:
                runInteractiveMode();
                break;
            default:
                std::cout << "Invalid choice. Please try again.\n";
        }

        std::cout << "\nPress Enter to continue...";
        std::cin.get();
    }

    return 0;
}
