/**
 * @file ThreadedHSM.hpp
 * @brief Threaded Hierarchical State Machine with Events and Commands
 *
 * This is a complete HSM implementation with:
 * - Hierarchical (nested) states using std::variant
 * - State entry/exit actions
 * - Event/Command-driven transitions using std::visit
 * - Type-safe state handling
 * - Events (past-tense notifications: "what happened")
 * - Commands (imperative instructions: "what to do")
 * - JSON message protocol for inter-thread communication
 * - Synchronous and asynchronous message execution
 * - Message buffering with futures/promises
 * - Thread-safe message queue
 *
 * The HSM runs in a dedicated worker thread, providing galvanic separation
 * between the main/UI thread and the state machine engine.
 *
 * Laser Tracker State Hierarchy:
 *
 *  [Off]
 *    |
 *    v (PowerOn)
 *  [Operational] ─────────────────────────────────────────┐
 *    │                                                     │
 *    ├── [Initializing] ──(InitComplete)──> [Idle]        │
 *    │         │                              │            │
 *    │         │(InitFailed)                  │(StartSearch)
 *    │         v                              v            │
 *    │      [Error] <──(ErrorOccurred)── [Tracking]       │
 *    │         │                              │            │
 *    │         │(Reset)                       ├── [Searching]
 *    │         v                              │      │     │
 *    │    [Initializing]                      │      │(TargetFound)
 *    │                                        │      v     │
 *    │                                        ├── [Locked] │
 *    │                                        │      │     │
 *    │                                        │      │(StartMeasure)
 *    │                                        │      v     │
 *    │                                        └── [Measuring]
 *    │                                                     │
 *    └─────────────────────────────────────────────────────┘
 *                          │
 *                          v (PowerOff)
 *                        [Off]
 *
 * JSON Message Protocol:
 * {
 *   "id": <unique_identifier>,
 *   "name": <command_name>,
 *   "params": { ... },
 *   "sync": true | false
 * }
 *
 * Response Format:
 * {
 *   "id": <same_identifier>,
 *   "success": true | false,
 *   "result": { ... } | null,
 *   "error": <error_message> | null
 * }
 */

#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <nlohmann/json.hpp>

namespace LaserTracker
{

    // ============================================================================
    // Forward Declarations
    // ============================================================================

    class HSM;

    // ============================================================================
    // Events - Past tense notifications of what happened (FSM reacts to these)
    // ============================================================================

    namespace Events
    {
        /** @brief Initialization completed successfully */
        struct InitComplete
        {
            std::string operator()() const { return "InitComplete"; }
        };
        /** @brief Initialization failed with error */
        struct InitFailed
        {
            std::string errorReason;
            std::string operator()() const { return "InitFailed: " + errorReason; }
        };
        /** @brief Target retroreflector was found */
        struct TargetFound
        {
            double      distance_mm;
            std::string operator()() const
            {
                std::ostringstream oss;
                oss << "TargetFound at " << std::fixed << std::setprecision(3) << distance_mm << " mm";
                return oss.str();
            }
        };
        /** @brief Target was lost during tracking */
        struct TargetLost
        {
            std::string operator()() const { return "TargetLost"; }
        };
        /** @brief A measurement point was recorded */
        struct MeasurementComplete
        {
            double      x, y, z;
            std::string operator()() const
            {
                std::ostringstream oss;
                oss << "MeasurementComplete: (" << std::fixed << std::setprecision(6) << x << ", " << y << ", " << z << ")";
                return oss.str();
            }
        };
        /** @brief An error occurred in the system */
        struct ErrorOccurred
        {
            int         errorCode;
            std::string description;
            std::string operator()() const { return "Error[" + std::to_string(errorCode) + "]: " + description; }
        };
    } // namespace Events

    // ============================================================================
    // Commands - Imperative instructions (what to do)
    // ============================================================================

    namespace Commands
    {
        // --------------------------------------------------------------------
        // State-Changing Commands
        // --------------------------------------------------------------------

        /** @brief Turn on the laser tracker power */
        struct PowerOn
        {
            static constexpr const char* name = "PowerOn";
            std::string                  operator()() const { return name; }
        };
        /** @brief Turn off the laser tracker power */
        struct PowerOff
        {
            static constexpr const char* name = "PowerOff";
            std::string                  operator()() const { return name; }
        };
        /** @brief Start searching for target */
        struct StartSearch
        {
            static constexpr const char* name = "StartSearch";
            std::string                  operator()() const { return name; }
        };
        /** @brief Start precision measurement */
        struct StartMeasure
        {
            static constexpr const char* name = "StartMeasure";
            std::string                  operator()() const { return name; }
        };
        /** @brief Stop measurement and return to locked */
        struct StopMeasure
        {
            static constexpr const char* name = "StopMeasure";
            std::string                  operator()() const { return name; }
        };
        /** @brief Reset the system from error state */
        struct Reset
        {
            static constexpr const char* name = "Reset";
            std::string                  operator()() const { return name; }
        };
        /** @brief Return from tracking to idle state */
        struct ReturnToIdle
        {
            static constexpr const char* name = "ReturnToIdle";
            std::string                  operator()() const { return name; }
        };

        // --------------------------------------------------------------------
        // Action Commands (don't change state, may be state-restricted)
        // --------------------------------------------------------------------

        /** @brief Home - moves to home position. Valid in: Idle. Sync: Yes */
        struct Home
        {
            static constexpr const char* name = "Home";
            static constexpr bool        sync = true;
            double                       speed = 100.0;
            std::string                  operator()() const { return name; }
        };
        /** @brief GetPosition - retrieves current position. Valid in: Idle, Locked, Measuring. Sync: No */
        struct GetPosition
        {
            static constexpr const char* name = "GetPosition";
            static constexpr bool        sync = false;
            std::string                  operator()() const { return name; }
        };
        /** @brief SetLaserPower - adjusts laser power. Valid in: Any Operational. Sync: No */
        struct SetLaserPower
        {
            static constexpr const char* name = "SetLaserPower";
            static constexpr bool        sync = false;
            double                       powerLevel = 1.0;
            std::string                  operator()() const { return name; }
        };
        /** @brief Compensate - applies environmental compensation. Valid in: Idle, Locked. Sync: Yes */
        struct Compensate
        {
            static constexpr const char* name = "Compensate";
            static constexpr bool        sync = true;
            double                       temperature = 20.0;
            double                       pressure    = 1013.25;
            double                       humidity    = 50.0;
            std::string                  operator()() const { return name; }
        };
        /** @brief GetStatus - retrieves system status. Valid in: Any. Sync: No */
        struct GetStatus
        {
            static constexpr const char* name = "GetStatus";
            static constexpr bool        sync = false;
            std::string                  operator()() const { return name; }
        };
        /** @brief MoveRelative - moves tracker by relative amount. Valid in: Idle, Locked. Sync: Yes */
        struct MoveRelative
        {
            static constexpr const char* name = "MoveRelative";
            static constexpr bool        sync = true;
            double                       azimuth   = 0.0;
            double                       elevation = 0.0;
            std::string                  operator()() const { return name; }
        };
    } // namespace Commands

    // ============================================================================
    // StateMessage - Unified variant for all Events and Commands
    // ============================================================================

    // Single flat variant containing all message types (Events and Commands)
    // The namespace distinction provides semantic clarity, but processing is uniform
    using StateMessage = std::variant<
        // Events (past tense - what happened)
        Events::InitComplete, Events::InitFailed, Events::TargetFound, Events::TargetLost, Events::MeasurementComplete, Events::ErrorOccurred,
        // Commands (imperative - what to do)
        Commands::PowerOn, Commands::PowerOff, Commands::StartSearch, Commands::StartMeasure, Commands::StopMeasure, Commands::Reset, Commands::ReturnToIdle,
        // Action Commands (don't change state)
        Commands::Home, Commands::GetPosition, Commands::SetLaserPower, Commands::Compensate, Commands::GetStatus, Commands::MoveRelative>;

    // Helper to get message name - works uniformly for all types via operator()
    inline std::string getMessageName(const StateMessage& msg)
    {
        return std::visit([](const auto& m) { return m(); }, msg);
    }

    // ============================================================================
    // States - Hierarchical state definitions using nested variants
    // ============================================================================

    namespace States
    {

        // ------------------------------------------------------------------------
        // Leaf States (no sub-states)
        // ------------------------------------------------------------------------

        struct Off
        {
            static constexpr const char* name = "Off";
            void onEntry() const { std::cout << "  [ENTRY] Off: Laser tracker powered down\n"; }
            void onExit() const { std::cout << "  [EXIT] Off: Preparing for power up\n"; }
        };
        struct Initializing
        {
            static constexpr const char* name     = "Initializing";
            int                          progress = 0;
            void onEntry() const { std::cout << "  [ENTRY] Initializing: Starting self-test and calibration\n"; }
            void onExit() const { std::cout << "  [EXIT] Initializing: Self-test complete\n"; }
            void updateProgress(int p)
            {
                progress = p;
                std::cout << "  [ACTION] Initialization progress: " << progress << "%\n";
            }
        };
        struct Idle
        {
            static constexpr const char* name = "Idle";
            void onEntry() const { std::cout << "  [ENTRY] Idle: Ready for operation, laser standby\n"; }
            void onExit() const { std::cout << "  [EXIT] Idle: Activating laser systems\n"; }
        };
        struct Error
        {
            static constexpr const char* name      = "Error";
            int                          errorCode = 0;
            std::string                  description;
            Error() = default;
            Error(int code, std::string desc) : errorCode(code), description(std::move(desc)) {}
            void onEntry() const { std::cout << "  [ENTRY] Error: System error detected - Code " << errorCode << ": " << description << "\n"; }
            void onExit() const { std::cout << "  [EXIT] Error: Error cleared, resuming operation\n"; }
        };

        // ------------------------------------------------------------------------
        // Tracking Sub-States (nested within Tracking composite state)
        // ------------------------------------------------------------------------

        struct Searching
        {
            static constexpr const char* name        = "Searching";
            double                       searchAngle = 0.0;
            void onEntry() const { std::cout << "  [ENTRY] Searching: Scanning for retroreflector target\n"; }
            void onExit() const { std::cout << "  [EXIT] Searching: Target acquisition complete\n"; }
            void updateSearchAngle(double angle)
            {
                searchAngle = angle;
                std::cout << "  [ACTION] Search angle: " << std::fixed << std::setprecision(1) << searchAngle << " degrees\n";
            }
        };
        struct Locked
        {
            static constexpr const char* name              = "Locked";
            double                       targetDistance_mm = 0.0;
            Locked()                                       = default;
            explicit Locked(double dist) : targetDistance_mm(dist) {}
            void onEntry() const { std::cout << "  [ENTRY] Locked: Target acquired at " << std::fixed << std::setprecision(3) << targetDistance_mm << " mm\n"; }
            void onExit() const { std::cout << "  [EXIT] Locked: Transitioning tracking mode\n"; }
        };
        struct Measuring
        {
            static constexpr const char* name             = "Measuring";
            int                          measurementCount = 0;
            double                       lastX = 0.0, lastY = 0.0, lastZ = 0.0;
            void onEntry() const { std::cout << "  [ENTRY] Measuring: Starting precision measurement\n"; }
            void onExit() const { std::cout << "  [EXIT] Measuring: Measurement session ended (" << measurementCount << " points recorded)\n"; }
            void recordMeasurement(double x, double y, double z)
            {
                lastX = x;
                lastY = y;
                lastZ = z;
                ++measurementCount;
                std::cout << "  [ACTION] Point #" << measurementCount << ": (" << std::fixed << std::setprecision(6) << x << ", " << y << ", " << z << ") mm\n";
            }
        };

        // Tracking sub-state variant
        using TrackingSubState = std::variant<Searching, Locked, Measuring>;

        // ------------------------------------------------------------------------
        // Composite States (contain sub-states)
        // ------------------------------------------------------------------------

        /**
         * @brief Tracking composite state - contains sub-states for tracking modes
         *
         * This demonstrates hierarchical state nesting with std::variant
         */
        struct Tracking
        {
            static constexpr const char* name = "Tracking";
            TrackingSubState             subState;
            Tracking() : subState(Searching{}) {}
            explicit Tracking(TrackingSubState sub) : subState(std::move(sub)) {}
            void onEntry() const
            {
                std::cout << "  [ENTRY] Tracking: Entering tracking mode\n";
                std::visit([](const auto& s) { s.onEntry(); }, subState);
            }
            void onExit() const
            {
                std::visit([](const auto& s) { s.onExit(); }, subState);
                std::cout << "  [EXIT] Tracking: Leaving tracking mode\n";
            }
            std::string getSubStateName() const
            {
                return std::visit([](const auto& s) -> std::string { return s.name; }, subState);
            }
        };

        // Operational sub-state variant
        using OperationalSubState = std::variant<Initializing, Idle, Tracking, Error>;

        /**
         * @brief Operational composite state - main operating super-state
         *
         * Contains all operational sub-states: Initializing, Idle, Tracking, Error
         */
        struct Operational
        {
            static constexpr const char* name = "Operational";
            OperationalSubState          subState;
            Operational() : subState(Initializing{}) {}
            explicit Operational(OperationalSubState sub) : subState(std::move(sub)) {}
            void onEntry() const
            {
                std::cout << "  [ENTRY] Operational: System powered on\n";
                std::visit([](const auto& s) { s.onEntry(); }, subState);
            }
            void onExit() const
            {
                std::visit([](const auto& s) { s.onExit(); }, subState);
                std::cout << "  [EXIT] Operational: Shutting down systems\n";
            }
            std::string getSubStateName() const
            {
                return std::visit(
                    [](const auto& s) -> std::string
                    {
                        using T = std::decay_t<decltype(s)>;
                        if constexpr (std::is_same_v<T, Tracking>)
                        {
                            return std::string(s.name) + "::" + s.getSubStateName();
                        }
                        else
                        {
                            return s.name;
                        }
                    },
                    subState);
            }
        };
    } // namespace States

    // Top-level state variant
    using State = std::variant<States::Off, States::Operational>;

    // ============================================================================
    // HSM - Hierarchical State Machine (internal implementation)
    // ============================================================================

    /**
     * @brief Internal Hierarchical State Machine for Laser Tracker
     *
     * Uses std::variant for type-safe state representation and std::visit
     * for command dispatching with proper entry/exit action handling.
     *
     * Note: This class is used internally by ThreadedHSM. For thread-safe
     * access, use the ThreadedHSM wrapper class.
     */
    class HSM
    {
      public:
        HSM() : currentState_(States::Off{})
        {
            std::cout << "=== Laser Tracker HSM Initialized ===\n";
            std::visit([](const auto& s) { s.onEntry(); }, currentState_);
        }

        /**
         * @brief Process a state message and perform state transition if applicable
         * @param msg The state message to process (Event or Command)
         * @return true if a transition occurred, false otherwise
         */
        bool processMessage(const StateMessage& msg)
        {
            std::cout << "\n>>> Message: " << getMessageName(msg) << "\n";

            bool transitioned = std::visit([this, &msg](auto& state) -> bool { return this->handleMessage(state, msg); }, currentState_);

            if (!transitioned)
            {
                std::cout << "  (Message ignored in current state)\n";
            }

            return transitioned;
        }

        /**
         * @brief Get the current state name (including hierarchy)
         */
        std::string getCurrentStateName() const
        {
            return std::visit(
                [](const auto& s) -> std::string
                {
                    using T = std::decay_t<decltype(s)>;
                    if constexpr (std::is_same_v<T, States::Operational>)
                    {
                        return std::string(s.name) + "::" + s.getSubStateName();
                    }
                    else
                    {
                        return s.name;
                    }
                },
                currentState_);
        }

        /**
         * @brief Check if in a specific top-level state type
         */
        template <typename S> bool isInState() const { return std::holds_alternative<S>(currentState_); }

        /**
         * @brief Get reference to current state (for testing/inspection)
         */
        const State& getState() const { return currentState_; }
        State&       getState() { return currentState_; }

        /**
         * @brief Print current state
         */
        void printState() const { std::cout << "Current State: [" << getCurrentStateName() << "]\n"; }

      private:
        State currentState_;

        // ------------------------------------------------------------------------
        // Transition helper - handles entry/exit actions
        // ------------------------------------------------------------------------

        template <typename NewState> void transitionTo(NewState newState)
        {
            // Exit current state
            std::visit([](auto& s) { s.onExit(); }, currentState_);

            // Enter new state
            currentState_ = std::move(newState);
            std::visit([](auto& s) { s.onEntry(); }, currentState_);
        }

        // Transition within Operational composite state
        template <typename NewSubState> void transitionOperationalTo(States::Operational& op, NewSubState newSub)
        {
            // Exit current sub-state
            std::visit([](auto& s) { s.onExit(); }, op.subState);

            // Enter new sub-state
            op.subState = std::move(newSub);
            std::visit([](auto& s) { s.onEntry(); }, op.subState);
        }

        // Transition within Tracking composite state
        template <typename NewSubState> void transitionTrackingTo(States::Tracking& tracking, NewSubState newSub)
        {
            // Exit current sub-state
            std::visit([](auto& s) { s.onExit(); }, tracking.subState);

            // Enter new sub-state
            tracking.subState = std::move(newSub);
            std::visit([](auto& s) { s.onEntry(); }, tracking.subState);
        }

        // ------------------------------------------------------------------------
        // Message handlers for each state - unified handling of Events and Commands
        // ------------------------------------------------------------------------

        // Handle messages in Off state
        bool handleMessage(States::Off& /*state*/, const StateMessage& msg)
        {
            return std::visit(
                [this](const auto& m) -> bool
                {
                    using M = std::decay_t<decltype(m)>;

                    if constexpr (std::is_same_v<M, Commands::PowerOn>)
                    {
                        transitionTo(States::Operational{});
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                },
                msg);
        }

        // Handle messages in Operational state (with sub-state dispatch)
        bool handleMessage(States::Operational& state, const StateMessage& msg)
        {
            // First, check for messages handled at Operational level
            bool handled = std::visit(
                [this, &state](const auto& m) -> bool
                {
                    using M = std::decay_t<decltype(m)>;

                    if constexpr (std::is_same_v<M, Commands::PowerOff>)
                    {
                        state.onExit(); // Exit Operational (which exits sub-state)
                        currentState_ = States::Off{};
                        std::get<States::Off>(currentState_).onEntry();
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                },
                msg);

            if (handled)
                return true;

            // Dispatch to sub-state handler
            return std::visit([this, &state, &msg](auto& subState) -> bool { return this->handleOperationalSubMessage(state, subState, msg); }, state.subState);
        }

        // Handle messages in Initializing sub-state
        bool handleOperationalSubMessage(States::Operational& parent, States::Initializing& /*state*/, const StateMessage& msg)
        {
            return std::visit(
                [this, &parent](const auto& m) -> bool
                {
                    using M = std::decay_t<decltype(m)>;

                    if constexpr (std::is_same_v<M, Events::InitComplete>)
                    {
                        transitionOperationalTo(parent, States::Idle{});
                        return true;
                    }
                    else if constexpr (std::is_same_v<M, Events::InitFailed>)
                    {
                        transitionOperationalTo(parent, States::Error{-1, m.errorReason});
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                },
                msg);
        }

        // Handle messages in Idle sub-state
        bool handleOperationalSubMessage(States::Operational& parent, States::Idle& /*state*/, const StateMessage& msg)
        {
            return std::visit(
                [this, &parent](const auto& m) -> bool
                {
                    using M = std::decay_t<decltype(m)>;

                    if constexpr (std::is_same_v<M, Commands::StartSearch>)
                    {
                        transitionOperationalTo(parent, States::Tracking{});
                        return true;
                    }
                    else if constexpr (std::is_same_v<M, Events::ErrorOccurred>)
                    {
                        transitionOperationalTo(parent, States::Error{m.errorCode, m.description});
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                },
                msg);
        }

        // Handle messages in Tracking sub-state (with its own sub-states)
        bool handleOperationalSubMessage(States::Operational& parent, States::Tracking& state, const StateMessage& msg)
        {
            // First check for messages handled at Tracking level
            bool handled = std::visit(
                [this, &parent, &state](const auto& m) -> bool
                {
                    using M = std::decay_t<decltype(m)>;

                    if constexpr (std::is_same_v<M, Commands::ReturnToIdle>)
                    {
                        transitionOperationalTo(parent, States::Idle{});
                        return true;
                    }
                    else if constexpr (std::is_same_v<M, Events::ErrorOccurred>)
                    {
                        // Exit Tracking (which exits its sub-state)
                        state.onExit();
                        parent.subState = States::Error{m.errorCode, m.description};
                        std::get<States::Error>(parent.subState).onEntry();
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                },
                msg);

            if (handled)
                return true;

            // Dispatch to Tracking sub-state handler
            return std::visit([this, &parent, &state, &msg](auto& subState) -> bool { return this->handleTrackingSubMessage(parent, state, subState, msg); },
                              state.subState);
        }

        // Handle messages in Error sub-state
        bool handleOperationalSubMessage(States::Operational& parent, States::Error& /*state*/, const StateMessage& msg)
        {
            return std::visit(
                [this, &parent](const auto& m) -> bool
                {
                    using M = std::decay_t<decltype(m)>;

                    if constexpr (std::is_same_v<M, Commands::Reset>)
                    {
                        // Reset goes back to Initializing
                        transitionOperationalTo(parent, States::Initializing{});
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                },
                msg);
        }

        // Handle messages in Searching sub-state
        bool handleTrackingSubMessage(States::Operational& /*parent*/, States::Tracking& tracking, States::Searching& /*state*/, const StateMessage& msg)
        {
            return std::visit(
                [this, &tracking](const auto& m) -> bool
                {
                    using M = std::decay_t<decltype(m)>;

                    if constexpr (std::is_same_v<M, Events::TargetFound>)
                    {
                        transitionTrackingTo(tracking, States::Locked{m.distance_mm});
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                },
                msg);
        }

        // Handle messages in Locked sub-state
        bool handleTrackingSubMessage(States::Operational& /*parent*/, States::Tracking& tracking, States::Locked& /*state*/, const StateMessage& msg)
        {
            return std::visit(
                [this, &tracking](const auto& m) -> bool
                {
                    using M = std::decay_t<decltype(m)>;

                    if constexpr (std::is_same_v<M, Commands::StartMeasure>)
                    {
                        transitionTrackingTo(tracking, States::Measuring{});
                        return true;
                    }
                    else if constexpr (std::is_same_v<M, Events::TargetLost>)
                    {
                        transitionTrackingTo(tracking, States::Searching{});
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                },
                msg);
        }

        // Handle messages in Measuring sub-state
        bool handleTrackingSubMessage(States::Operational& /*parent*/, States::Tracking& tracking, States::Measuring& state, const StateMessage& msg)
        {
            return std::visit(
                [this, &tracking, &state](const auto& m) -> bool
                {
                    using M = std::decay_t<decltype(m)>;

                    if constexpr (std::is_same_v<M, Events::MeasurementComplete>)
                    {
                        state.recordMeasurement(m.x, m.y, m.z);
                        return true;
                    }
                    else if constexpr (std::is_same_v<M, Commands::StopMeasure>)
                    {
                        transitionTrackingTo(tracking, States::Locked{});
                        return true;
                    }
                    else if constexpr (std::is_same_v<M, Events::TargetLost>)
                    {
                        transitionTrackingTo(tracking, States::Searching{});
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                },
                msg);
        }
    };

    // ============================================================================
    // JSON Type Alias
    // ============================================================================

    // JSON type alias (using nlohmann/json)
    using Json = nlohmann::json;

    // ============================================================================
    // Unified Message Type
    // ============================================================================

    /**
     * @brief Unified message for both requests and responses
     *
     * Request fields:
     *   - id: Unique identifier for correlation
     *   - name: Name of the command
     *   - params: Parameters for the message
     *   - sync: If true, sender waits for result before processing next
     *   - timeoutMs: Timeout in milliseconds for reply (0 = no timeout)
     *   - timestamp: Creation time of the message
     *
     * Response fields (when isResponse=true):
     *   - success: True if executed successfully
     *   - result: Result data (stored in params)
     *   - error: Error message if failed
     *
     * The HSM determines whether a message triggers a state change or not.
     * Commands can also cause state changes (e.g., error conditions).
     */
    struct Message
    {
        using Clock     = std::chrono::steady_clock;
        using TimePoint = std::chrono::time_point<Clock>;

        uint64_t    id = 0;            // Unique identifier for correlation
        std::string name;              // Name of command
        Json        params;            // Parameters (request) or result data (response)
        bool        sync       = false; // If true, sender waits for completion
        bool        needsReply = false; // If true, a response is expected
        uint32_t    timeoutMs  = 5000;  // Timeout in ms for reply (0 = no timeout)
        TimePoint   timestamp;          // When the message was created

        // Response-specific fields
        bool        isResponse = false; // True if this is a response message
        bool        success    = false; // True if executed successfully
        std::string error;              // Error message (if failed)

        Message() : timestamp(Clock::now()) {}

        /**
         * @brief Check if the message has timed out
         */
        bool isTimedOut() const
        {
            if (timeoutMs == 0)
                return false;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - timestamp);
            return elapsed.count() > timeoutMs;
        }

        /**
         * @brief Get remaining time until timeout
         */
        std::chrono::milliseconds remainingTime() const
        {
            if (timeoutMs == 0)
                return std::chrono::milliseconds::max();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - timestamp);
            auto remaining = static_cast<int64_t>(timeoutMs) - elapsed.count();
            return std::chrono::milliseconds(std::max<int64_t>(0, remaining));
        }

        /**
         * @brief Get age of message in milliseconds
         */
        uint64_t ageMs() const
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - timestamp).count();
        }

        /**
         * @brief Create a response from a request
         */
        static Message createResponse(uint64_t requestId, bool success, Json result = {}, const std::string& error = "")
        {
            Message resp;
            resp.id         = requestId;
            resp.isResponse = true;
            resp.success    = success;
            resp.params     = std::move(result);
            resp.error      = error;
            return resp;
        }

        /**
         * @brief Create a timeout error response
         */
        static Message createTimeoutResponse(uint64_t requestId)
        {
            return createResponse(requestId, false, {}, "Request timed out");
        }

        std::string toJson() const
        {
            Json obj;
            obj["id"]           = id;
            obj["name"]         = name;
            obj["timestamp_ms"] = ageMs();

            if (isResponse)
            {
                obj["isResponse"] = true;
                obj["success"]    = success;
                obj["result"]     = params;
                if (!error.empty())
                {
                    obj["error"] = error;
                }
            }
            else
            {
                obj["params"]    = params;
                obj["sync"]      = sync;
                obj["timeoutMs"] = timeoutMs;
            }
            return obj.dump();
        }
    };

    // ============================================================================
    // Thread-Safe Message Queue
    // ============================================================================

    /**
     * @brief Thread-safe FIFO queue for messages
     */
    template <typename T> class ThreadSafeQueue
    {
      public:
        void push(T item)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push_back(std::move(item));
            }
            cv_.notify_one();
        }

        void pushFront(T item)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push_front(std::move(item));
            }
            cv_.notify_one();
        }

        std::optional<T> tryPop()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty())
            {
                return std::nullopt;
            }
            T item = std::move(queue_.front());
            queue_.pop_front();
            return item;
        }

        T waitPop()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || stopped_; });
            if (stopped_ && queue_.empty())
            {
                throw std::runtime_error("Queue stopped");
            }
            T item = std::move(queue_.front());
            queue_.pop_front();
            return item;
        }

        std::optional<T> waitPopFor(std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty() || stopped_; }))
            {
                return std::nullopt;
            }
            if (stopped_ && queue_.empty())
            {
                return std::nullopt;
            }
            T item = std::move(queue_.front());
            queue_.pop_front();
            return item;
        }

        void stop()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopped_ = true;
            }
            cv_.notify_all();
        }

        bool empty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.empty();
        }

        size_t size() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.size();
        }

        void clear()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.clear();
        }

      private:
        mutable std::mutex      mutex_;
        std::condition_variable cv_;
        std::deque<T>           queue_;
        bool                    stopped_ = false;
    };

    // ============================================================================
    // Pending Message Entry
    // ============================================================================

    struct PendingMessage
    {
        Message                   message;
        std::promise<Message>     promise; // For sync messages waiting for response

        PendingMessage() = default;
        PendingMessage(Message msg) : message(std::move(msg)) {}
    };

    // ============================================================================
    // Threaded HSM - State Machine running in its own thread
    // ============================================================================

    /**
     * @brief Threaded HSM with Events and Commands support and JSON messaging
     *
     * The HSM runs in a dedicated thread, receiving messages (events/commands)
     * via a thread-safe message queue. Responses are sent back through
     * a separate queue or via futures for synchronous messages.
     *
     * Message types:
     * - Events (past tense): Notifications of what happened (InitComplete, TargetFound, etc.)
     * - Commands (imperative): Instructions to execute (PowerOn, StartSearch, Home, etc.)
     *
     * Both Events and Commands can trigger state transitions. The distinction is semantic.
     */
    class ThreadedHSM
    {
      public:
        ThreadedHSM() : running_(false), nextMessageId_(1), syncMessageInProgress_(false) { std::cout << "=== Threaded Laser Tracker HSM Created ===\n"; }

        ~ThreadedHSM() { stop(); }

        /**
         * @brief Start the HSM thread
         */
        void start()
        {
            if (running_.exchange(true))
            {
                return; // Already running
            }

            workerThread_ = std::thread(&ThreadedHSM::workerLoop, this);

#ifdef _WIN32
            // Set thread name for debugger visibility
            SetThreadDescription(workerThread_.native_handle(), L"LaserTracker HSM Worker");
#endif

            std::cout << "[ThreadedHSM] Worker thread started\n";
        }

        /**
         * @brief Stop the HSM thread
         */
        void stop()
        {
            if (!running_.exchange(false))
            {
                return; // Already stopped
            }

            messageQueue_.stop();
            if (workerThread_.joinable())
            {
                workerThread_.join();
            }
            std::cout << "[ThreadedHSM] Worker thread stopped\n";
        }

        /**
         * @brief Check if HSM thread is running
         */
        bool isRunning() const { return running_.load(); }

        // --------------------------------------------------------------------
        // Unified Message Sending Interface
        // --------------------------------------------------------------------

        /**
         * @brief Send a message asynchronously (fire and forget)
         * @return Message ID for tracking
         */
        uint64_t sendAsync(const std::string& name, Json params = {}, bool sync = false)
        {
            Message msg;
            msg.id         = nextMessageId_++;
            msg.name       = name;
            msg.params     = std::move(params);
            msg.sync       = sync;
            msg.needsReply = false;
            msg.timeoutMs  = 0; // No timeout for async

            queueMessage(std::move(msg));
            return msg.id;
        }

        /**
         * @brief Send a message and wait for response
         * @param name Message name
         * @param params Message parameters
         * @param sync If true, HSM blocks other messages until this completes
         * @param timeoutMs Timeout in milliseconds (0 = no timeout)
         * @return Response message
         */
        Message send(const std::string& name, Json params = {}, bool sync = false, uint32_t timeoutMs = 30000)
        {
            Message msg;
            msg.id         = nextMessageId_++;
            msg.name       = name;
            msg.params     = std::move(params);
            msg.sync       = sync;
            msg.needsReply = true;
            msg.timeoutMs  = timeoutMs;
            // timestamp is set automatically in constructor

            return sendAndWait(std::move(msg));
        }

        // --------------------------------------------------------------------
        // Convenience methods for StateMessages (Events and Commands)
        // --------------------------------------------------------------------

        /**
         * @brief Send a message asynchronously (fire and forget)
         */
        uint64_t sendMessageAsync(const StateMessage& msg)
        {
            return sendAsync(getMessageName(msg), messageToParams(msg), isMessageSync(msg));
        }

        /**
         * @brief Send a message and wait for response
         */
        Message sendMessage(const StateMessage& msg, uint32_t timeoutMs = 30000)
        {
            return send(getMessageName(msg), messageToParams(msg), isMessageSync(msg), timeoutMs);
        }

        // --------------------------------------------------------------------
        // JSON Message Interface
        // --------------------------------------------------------------------

        /**
         * @brief Send a raw JSON message
         * @return Message ID
         */
        uint64_t sendJsonMessage(const std::string& json)
        {
            Message msg = parseJsonMessage(json);
            if (msg.id == 0)
            {
                msg.id = nextMessageId_++;
            }
            queueMessage(std::move(msg));
            return msg.id;
        }

        /**
         * @brief Get response from async response queue
         */
        std::optional<Message> tryGetResponse() { return responseQueue_.tryPop(); }

        /**
         * @brief Wait for a specific response
         */
        std::optional<Message> waitForResponse(uint64_t messageId, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
        {
            auto deadline = std::chrono::steady_clock::now() + timeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                auto response  = responseQueue_.waitPopFor(remaining);
                if (response && response->id == messageId)
                {
                    return response;
                }
                // Put back if not our response
                if (response)
                {
                    responseQueue_.pushFront(std::move(*response));
                }
            }
            return std::nullopt;
        }

        // --------------------------------------------------------------------
        // State Query Interface (thread-safe)
        // --------------------------------------------------------------------

        /**
         * @brief Get current state name (thread-safe)
         */
        std::string getCurrentStateName() const
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            return hsm_.getCurrentStateName();
        }

        /**
         * @brief Check if in a specific state type
         */
        template <typename S> bool isInState() const
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            return hsm_.isInState<S>();
        }

      private:
        HSM                           hsm_;
        std::thread                   workerThread_;
        std::atomic<bool>             running_;
        std::atomic<uint64_t>         nextMessageId_;
        mutable std::mutex            stateMutex_;

        // Message queues
        ThreadSafeQueue<PendingMessage> messageQueue_;
        ThreadSafeQueue<Message>        responseQueue_;

        // Buffered messages (waiting for sync message to complete)
        std::deque<PendingMessage> messageBuffer_;
        std::atomic<bool>          syncMessageInProgress_;
        std::mutex                 bufferMutex_;

        // Pending sync requests (id -> promise)
        std::unordered_map<uint64_t, std::promise<Message>> pendingPromises_;
        std::mutex                                          promiseMutex_;

        // --------------------------------------------------------------------
        // Worker Thread Loop
        // --------------------------------------------------------------------

        void workerLoop()
        {
            std::cout << "[HSM Thread] Started\n";

            while (running_.load())
            {
                try
                {
                    auto pending = messageQueue_.waitPopFor(std::chrono::milliseconds(100));
                    if (!pending)
                    {
                        continue;
                    }

                    processMessage(std::move(*pending));
                }
                catch (const std::exception& e)
                {
                    std::cerr << "[HSM Thread] Exception: " << e.what() << "\n";
                }
            }

            std::cout << "[HSM Thread] Stopped\n";
        }

        void processMessage(PendingMessage pending)
        {
            const Message& msg = pending.message;

            std::cout << "\n[HSM Thread] Processing: '" << msg.name << "' (id=" << msg.id << ", sync=" << (msg.sync ? "true" : "false") << ", age=" << msg.ageMs()
                      << "ms)\n";

            // Check if message has already timed out
            if (msg.needsReply && msg.isTimedOut())
            {
                std::cout << "[HSM Thread] Message timed out before processing (age=" << msg.ageMs() << "ms, timeout=" << msg.timeoutMs << "ms)\n";
                // The sender has already timed out, no point processing
                // But still need to clean up any pending promise
                {
                    std::lock_guard<std::mutex> lock(promiseMutex_);
                    pendingPromises_.erase(msg.id);
                }
                return;
            }

            // If a sync message is in progress, buffer this message
            if (syncMessageInProgress_.load() && msg.sync)
            {
                std::cout << "[HSM Thread] Sync message in progress, buffering\n";
                std::lock_guard<std::mutex> lock(bufferMutex_);
                messageBuffer_.push_back(std::move(pending));
                return;
            }

            // Mark sync in progress if this is a sync message
            if (msg.sync)
            {
                syncMessageInProgress_.store(true);
            }

            // Process the message - try as state command first, then as action command
            Message response = processMessageContent(msg);

            // Send response if needed
            if (msg.needsReply)
            {
                // Check if there's a promise waiting
                {
                    std::lock_guard<std::mutex> lock(promiseMutex_);
                    auto                        it = pendingPromises_.find(msg.id);
                    if (it != pendingPromises_.end())
                    {
                        it->second.set_value(response);
                        pendingPromises_.erase(it);
                    }
                    else
                    {
                        // Otherwise put in response queue
                        responseQueue_.push(std::move(response));
                    }
                }
            }

            // If this was a sync message and it completed, process buffered messages
            if (msg.sync)
            {
                syncMessageInProgress_.store(false);
                processBufferedMessages();
            }
        }

        void processBufferedMessages()
        {
            std::deque<PendingMessage> toProcess;
            {
                std::lock_guard<std::mutex> lock(bufferMutex_);
                toProcess.swap(messageBuffer_);
            }

            if (!toProcess.empty())
            {
                std::cout << "[HSM Thread] Processing " << toProcess.size() << " buffered messages\n";
            }

            for (auto& pending : toProcess)
            {
                // Skip timed-out messages in buffer
                if (pending.message.needsReply && pending.message.isTimedOut())
                {
                    std::cout << "[HSM Thread] Skipping timed-out buffered message: " << pending.message.name << " (id=" << pending.message.id << ")\n";
                    std::lock_guard<std::mutex> lock(promiseMutex_);
                    pendingPromises_.erase(pending.message.id);
                    continue;
                }
                processMessage(std::move(pending));
            }
        }

        /**
         * @brief Process a message - determines if it's a state message or action command
         */
        Message processMessageContent(const Message& msg)
        {
            // First, try to parse as a StateMessage (Event or state-changing Command)
            auto stateMsg = paramsToStateMessage(msg.name, msg.params);
            if (stateMsg)
            {
                return processStateMessage(msg, *stateMsg);
            }

            // Otherwise, process as an action command
            return processActionCommand(msg);
        }

        // --------------------------------------------------------------------
        // StateMessage Processing (Events and state-changing Commands)
        // --------------------------------------------------------------------

        Message processStateMessage(const Message& msg, const StateMessage& stateMsg)
        {
            // Process the state message
            bool handled;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                handled = hsm_.processMessage(stateMsg);
            }

            Json result;
            result["handled"]      = handled;
            result["state"]        = getCurrentStateName();
            result["stateChanged"] = handled;

            if (!handled)
            {
                return Message::createResponse(msg.id, false, result, "Message not handled in current state");
            }

            return Message::createResponse(msg.id, true, result);
        }

        // --------------------------------------------------------------------
        // Action Command Processing
        // --------------------------------------------------------------------

        Message processActionCommand(const Message& msg)
        {
            // Check if command is valid in current state
            std::string currentState = getCurrentStateName();

            // Execute command based on name
            if (msg.name == Commands::Home::name)
            {
                return executeHome(msg, currentState);
            }
            else if (msg.name == Commands::GetPosition::name)
            {
                return executeGetPosition(msg, currentState);
            }
            else if (msg.name == Commands::SetLaserPower::name)
            {
                return executeSetLaserPower(msg, currentState);
            }
            else if (msg.name == Commands::Compensate::name)
            {
                return executeCompensate(msg, currentState);
            }
            else if (msg.name == Commands::GetStatus::name)
            {
                return executeGetStatus(msg, currentState);
            }
            else if (msg.name == Commands::MoveRelative::name)
            {
                return executeMoveRelative(msg, currentState);
            }
            else
            {
                return Message::createResponse(msg.id, false, {}, "Unknown message: " + msg.name);
            }
        }

        // --------------------------------------------------------------------
        // Command Executors
        // --------------------------------------------------------------------

        Message executeHome(const Message& msg, const std::string& currentState)
        {
            // Home is only valid in Idle state
            if (currentState.find("Idle") == std::string::npos)
            {
                return Message::createResponse(msg.id, false, {}, "Home command only valid in Idle state (current: " + currentState + ")");
            }

            double speed = 100.0;
            if (msg.params.contains("speed"))
            {
                speed = msg.params.at("speed").get<double>();
            }

            std::cout << "  [COMMAND] Home: Moving to home position at " << speed << "% speed\n";

            // Simulate homing operation (sync)
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(1000 / (speed / 100.0))));

            std::cout << "  [COMMAND] Home: Homing complete\n";

            Json result;
            result["position"]["azimuth"]   = 0.0;
            result["position"]["elevation"] = 0.0;
            result["state"] = getCurrentStateName();

            return Message::createResponse(msg.id, true, result);
        }

        Message executeGetPosition(const Message& msg, const std::string& currentState)
        {
            // GetPosition valid in Idle, Locked, Measuring
            if (currentState.find("Off") != std::string::npos || currentState.find("Initializing") != std::string::npos ||
                currentState.find("Error") != std::string::npos)
            {
                return Message::createResponse(msg.id, false, {}, "GetPosition not available in " + currentState);
            }

            // Simulate reading position
            Json result;
            result["position"]["x"]         = 1234.567;
            result["position"]["y"]         = 2345.678;
            result["position"]["z"]         = 345.789;
            result["position"]["azimuth"]   = 45.123;
            result["position"]["elevation"] = 12.456;

            std::cout << "  [COMMAND] GetPosition: Returned current position\n";

            return Message::createResponse(msg.id, true, result);
        }

        Message executeSetLaserPower(const Message& msg, const std::string& currentState)
        {
            // SetLaserPower valid in any Operational state
            if (currentState.find("Off") != std::string::npos)
            {
                return Message::createResponse(msg.id, false, {}, "SetLaserPower not available when powered off");
            }

            double power = 1.0;
            if (msg.params.contains("powerLevel"))
            {
                power = msg.params.at("powerLevel").get<double>();
            }

            if (power < 0.0 || power > 1.0)
            {
                return Message::createResponse(msg.id, false, {}, "Power level must be between 0.0 and 1.0");
            }

            std::cout << "  [COMMAND] SetLaserPower: Set to " << (power * 100) << "%\n";

            Json result;
            result["powerLevel"] = power;

            return Message::createResponse(msg.id, true, result);
        }

        Message executeCompensate(const Message& msg, const std::string& currentState)
        {
            // Compensate valid in Idle, Locked
            if (currentState.find("Idle") == std::string::npos && currentState.find("Locked") == std::string::npos)
            {
                return Message::createResponse(msg.id, false, {}, "Compensate only valid in Idle or Locked state");
            }

            double temp     = 20.0;
            double pressure = 1013.25;
            double humidity = 50.0;

            if (msg.params.contains("temperature"))
                temp = msg.params.at("temperature").get<double>();
            if (msg.params.contains("pressure"))
                pressure = msg.params.at("pressure").get<double>();
            if (msg.params.contains("humidity"))
                humidity = msg.params.at("humidity").get<double>();

            std::cout << "  [COMMAND] Compensate: Applying environmental compensation\n";
            std::cout << "            T=" << temp << "C, P=" << pressure << "hPa, H=" << humidity << "%\n";

            // Simulate calculation (sync)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Calculate compensation factor (simplified)
            double factor = 1.0 + ((temp - 20.0) * 0.000001) + ((pressure - 1013.25) * 0.0000001);

            std::cout << "  [COMMAND] Compensate: Factor = " << std::fixed << std::setprecision(8) << factor << "\n";

            Json result;
            result["compensationFactor"] = factor;
            result["applied"]            = true;

            return Message::createResponse(msg.id, true, result);
        }

        Message executeGetStatus(const Message& msg, const std::string& currentState)
        {
            Json result;
            result["state"]   = currentState;
            result["healthy"] = (currentState.find("Error") == std::string::npos);
            result["powered"] = (currentState.find("Off") == std::string::npos);

            std::cout << "  [COMMAND] GetStatus: State=" << currentState << "\n";

            return Message::createResponse(msg.id, true, result);
        }

        Message executeMoveRelative(const Message& msg, const std::string& currentState)
        {
            // MoveRelative valid in Idle, Locked
            if (currentState.find("Idle") == std::string::npos && currentState.find("Locked") == std::string::npos)
            {
                return Message::createResponse(msg.id, false, {}, "MoveRelative only valid in Idle or Locked state");
            }

            double azimuth   = 0.0;
            double elevation = 0.0;

            if (msg.params.contains("azimuth"))
                azimuth = msg.params.at("azimuth").get<double>();
            if (msg.params.contains("elevation"))
                elevation = msg.params.at("elevation").get<double>();

            std::cout << "  [COMMAND] MoveRelative: Moving by az=" << azimuth << ", el=" << elevation << "\n";

            // Simulate move (sync)
            double moveTime = std::sqrt(azimuth * azimuth + elevation * elevation) * 10;
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(moveTime)));

            std::cout << "  [COMMAND] MoveRelative: Move complete\n";

            Json result;
            result["movedAz"]    = azimuth;
            result["movedEl"]    = elevation;
            result["moveTimeMs"] = static_cast<int>(moveTime);

            return Message::createResponse(msg.id, true, result);
        }

        // --------------------------------------------------------------------
        // Helper Methods
        // --------------------------------------------------------------------

        void queueMessage(Message msg)
        {
            PendingMessage pending;
            pending.message = std::move(msg);
            messageQueue_.push(std::move(pending));
        }

        /**
         * @brief Send a message and wait for response using the message's timeout
         */
        Message sendAndWait(Message msg)
        {
            std::promise<Message> promise;
            auto                  future = promise.get_future();

            uint64_t id        = msg.id;
            uint32_t timeoutMs = msg.timeoutMs;

            {
                std::lock_guard<std::mutex> lock(promiseMutex_);
                pendingPromises_[id] = std::move(promise);
            }

            PendingMessage pending;
            pending.message = std::move(msg);
            messageQueue_.push(std::move(pending));

            // Use the message's remaining time for waiting
            std::future_status status;
            if (timeoutMs == 0)
            {
                // No timeout - wait indefinitely
                future.wait();
                status = std::future_status::ready;
            }
            else
            {
                status = future.wait_for(std::chrono::milliseconds(timeoutMs));
            }

            if (status == std::future_status::ready)
            {
                return future.get();
            }
            else
            {
                // Timeout - remove promise
                {
                    std::lock_guard<std::mutex> lock(promiseMutex_);
                    pendingPromises_.erase(id);
                }
                return Message::createTimeoutResponse(id);
            }
        }

        // --------------------------------------------------------------------
        // Conversion Helpers - Unified for all StateMessage types
        // --------------------------------------------------------------------

        /**
         * @brief Check if a message requires synchronous execution
         */
        static bool isMessageSync(const StateMessage& msg)
        {
            return std::visit(
                [](const auto& m) -> bool
                {
                    using M = std::decay_t<decltype(m)>;
                    // Only action commands have the sync flag
                    if constexpr (std::is_same_v<M, Commands::Home> || std::is_same_v<M, Commands::GetPosition> || std::is_same_v<M, Commands::SetLaserPower> ||
                                  std::is_same_v<M, Commands::Compensate> || std::is_same_v<M, Commands::GetStatus> || std::is_same_v<M, Commands::MoveRelative>)
                    {
                        return m.sync;
                    }
                    else
                    {
                        return false;
                    }
                },
                msg);
        }

        /**
         * @brief Convert a StateMessage to JSON params
         */
        static Json messageToParams(const StateMessage& msg)
        {
            return std::visit(
                [](const auto& m) -> Json
                {
                    using M = std::decay_t<decltype(m)>;
                    Json params = Json::object();

                    // Events
                    if constexpr (std::is_same_v<M, Events::TargetFound>)
                    {
                        params["distance_mm"] = m.distance_mm;
                    }
                    else if constexpr (std::is_same_v<M, Events::InitFailed>)
                    {
                        params["errorReason"] = m.errorReason;
                    }
                    else if constexpr (std::is_same_v<M, Events::MeasurementComplete>)
                    {
                        params["x"] = m.x;
                        params["y"] = m.y;
                        params["z"] = m.z;
                    }
                    else if constexpr (std::is_same_v<M, Events::ErrorOccurred>)
                    {
                        params["errorCode"]   = m.errorCode;
                        params["description"] = m.description;
                    }
                    // Action Commands
                    else if constexpr (std::is_same_v<M, Commands::Home>)
                    {
                        params["speed"] = m.speed;
                    }
                    else if constexpr (std::is_same_v<M, Commands::SetLaserPower>)
                    {
                        params["powerLevel"] = m.powerLevel;
                    }
                    else if constexpr (std::is_same_v<M, Commands::Compensate>)
                    {
                        params["temperature"] = m.temperature;
                        params["pressure"]    = m.pressure;
                        params["humidity"]    = m.humidity;
                    }
                    else if constexpr (std::is_same_v<M, Commands::MoveRelative>)
                    {
                        params["azimuth"]   = m.azimuth;
                        params["elevation"] = m.elevation;
                    }

                    return params;
                },
                msg);
        }

        /**
         * @brief Parse message name/params into a StateMessage (Events or state-changing Commands)
         */
        static std::optional<StateMessage> paramsToStateMessage(const std::string& name, const Json& params)
        {
            // Events (past tense)
            if (name == "InitComplete" || name.find("InitComplete") != std::string::npos)
            {
                return Events::InitComplete{};
            }
            else if (name == "InitFailed" || name.find("InitFailed") != std::string::npos)
            {
                Events::InitFailed e;
                if (params.contains("errorReason"))
                {
                    e.errorReason = params.at("errorReason").get<std::string>();
                }
                return e;
            }
            else if (name == "TargetFound" || name.find("TargetFound") != std::string::npos)
            {
                Events::TargetFound e;
                if (params.contains("distance_mm"))
                {
                    e.distance_mm = params.at("distance_mm").get<double>();
                }
                return e;
            }
            else if (name == "TargetLost" || name.find("TargetLost") != std::string::npos)
            {
                return Events::TargetLost{};
            }
            else if (name == "MeasurementComplete" || name.find("MeasurementComplete") != std::string::npos)
            {
                Events::MeasurementComplete e;
                if (params.contains("x"))
                    e.x = params.at("x").get<double>();
                if (params.contains("y"))
                    e.y = params.at("y").get<double>();
                if (params.contains("z"))
                    e.z = params.at("z").get<double>();
                return e;
            }
            else if (name == "ErrorOccurred" || name.find("ErrorOccurred") != std::string::npos)
            {
                Events::ErrorOccurred e;
                if (params.contains("errorCode"))
                    e.errorCode = params.at("errorCode").get<int>();
                if (params.contains("description"))
                    e.description = params.at("description").get<std::string>();
                return e;
            }
            // Commands (imperative)
            else if (name == "PowerOn" || name.find("PowerOn") != std::string::npos)
            {
                return Commands::PowerOn{};
            }
            else if (name == "PowerOff" || name.find("PowerOff") != std::string::npos)
            {
                return Commands::PowerOff{};
            }
            else if (name == "StartSearch" || name.find("StartSearch") != std::string::npos)
            {
                return Commands::StartSearch{};
            }
            else if (name == "StartMeasure" || name.find("StartMeasure") != std::string::npos)
            {
                return Commands::StartMeasure{};
            }
            else if (name == "StopMeasure" || name.find("StopMeasure") != std::string::npos)
            {
                return Commands::StopMeasure{};
            }
            else if (name == "Reset" || name.find("Reset") != std::string::npos)
            {
                return Commands::Reset{};
            }
            else if (name == "ReturnToIdle" || name.find("ReturnToIdle") != std::string::npos)
            {
                return Commands::ReturnToIdle{};
            }

            return std::nullopt;
        }

        static Message parseJsonMessage(const std::string& jsonStr)
        {
            Message msg;

            try
            {
                Json json = Json::parse(jsonStr);

                if (json.contains("id") && json["id"].is_number())
                {
                    msg.id = json["id"].get<uint64_t>();
                }

                if (json.contains("name") && json["name"].is_string())
                {
                    msg.name = json["name"].get<std::string>();
                }

                if (json.contains("params"))
                {
                    msg.params = json["params"];
                }

                if (json.contains("sync") && json["sync"].is_boolean())
                {
                    msg.sync = json["sync"].get<bool>();
                }

                if (json.contains("needsReply") && json["needsReply"].is_boolean())
                {
                    msg.needsReply = json["needsReply"].get<bool>();
                }
                else
                {
                    msg.needsReply = msg.sync;
                }

                if (json.contains("timeoutMs") && json["timeoutMs"].is_number())
                {
                    msg.timeoutMs = json["timeoutMs"].get<uint32_t>();
                }
            }
            catch (const Json::parse_error& e)
            {
                std::cerr << "[parseJsonMessage] Parse error: " << e.what() << "\n";
            }

            return msg;
        }
    };

} // namespace LaserTracker
