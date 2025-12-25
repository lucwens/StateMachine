/**
 * @file LaserTrackerHSM.hpp
 * @brief Hierarchical State Machine (HSM) implementation for a Laser Tracker
 *        using C++17 std::variant pattern
 *
 * This demonstrates the HSM State pattern with:
 * - Hierarchical (nested) states using std::variant
 * - State entry/exit actions
 * - Event-driven transitions using std::visit
 * - Type-safe state handling
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
 */

#pragma once

#include <variant>
#include <string>
#include <iostream>
#include <optional>
#include <functional>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace LaserTracker
{

    // ============================================================================
    // Forward Declarations
    // ============================================================================

    struct HSM;

    // ============================================================================
    // Events - All possible events that can trigger state transitions
    // ============================================================================

    namespace Events
    {
        struct PowerOn
        {
            std::string operator()() const { return "PowerOn"; }
        };

        struct PowerOff
        {
            std::string operator()() const { return "PowerOff"; }
        };

        struct InitComplete
        {
            std::string operator()() const { return "InitComplete"; }
        };

        struct InitFailed
        {
            std::string errorReason;
            std::string operator()() const { return "InitFailed: " + errorReason; }
        };

        struct StartSearch
        {
            std::string operator()() const { return "StartSearch"; }
        };

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

        struct TargetLost
        {
            std::string operator()() const { return "TargetLost"; }
        };

        struct StartMeasure
        {
            std::string operator()() const { return "StartMeasure"; }
        };

        struct StopMeasure
        {
            std::string operator()() const { return "StopMeasure"; }
        };

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

        struct ErrorOccurred
        {
            int         errorCode;
            std::string description;
            std::string operator()() const { return "Error[" + std::to_string(errorCode) + "]: " + description; }
        };

        struct Reset
        {
            std::string operator()() const { return "Reset"; }
        };

        struct ReturnToIdle
        {
            std::string operator()() const { return "ReturnToIdle"; }
        };
    } // namespace Events

    // Event variant - all possible events
    using Event =
        std::variant<Events::PowerOn, Events::PowerOff, Events::InitComplete, Events::InitFailed, Events::StartSearch, Events::TargetFound, Events::TargetLost,
                     Events::StartMeasure, Events::StopMeasure, Events::MeasurementComplete, Events::ErrorOccurred, Events::Reset, Events::ReturnToIdle>;

    // Helper to get event name
    inline std::string getEventName(const Event &event)
    {
        return std::visit([](const auto &e) { return e(); }, event);
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
            static constexpr const char *name = "Off";

            void onEntry() const { std::cout << "  [ENTRY] Off: Laser tracker powered down\n"; }

            void onExit() const { std::cout << "  [EXIT] Off: Preparing for power up\n"; }
        };

        struct Initializing
        {
            static constexpr const char *name     = "Initializing";
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
            static constexpr const char *name = "Idle";

            void onEntry() const { std::cout << "  [ENTRY] Idle: Ready for operation, laser standby\n"; }

            void onExit() const { std::cout << "  [EXIT] Idle: Activating laser systems\n"; }
        };

        struct Error
        {
            static constexpr const char *name      = "Error";
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
            static constexpr const char *name        = "Searching";
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
            static constexpr const char *name              = "Locked";
            double                       targetDistance_mm = 0.0;

            Locked()                                       = default;
            explicit Locked(double dist) : targetDistance_mm(dist) {}

            void onEntry() const { std::cout << "  [ENTRY] Locked: Target acquired at " << std::fixed << std::setprecision(3) << targetDistance_mm << " mm\n"; }

            void onExit() const { std::cout << "  [EXIT] Locked: Transitioning tracking mode\n"; }
        };

        struct Measuring
        {
            static constexpr const char *name             = "Measuring";
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
            static constexpr const char *name = "Tracking";
            TrackingSubState             subState;

            Tracking() : subState(Searching{}) {}
            explicit Tracking(TrackingSubState sub) : subState(std::move(sub)) {}

            void onEntry() const
            {
                std::cout << "  [ENTRY] Tracking: Entering tracking mode\n";
                // Also enter the initial sub-state
                std::visit([](const auto &s) { s.onEntry(); }, subState);
            }

            void onExit() const
            {
                // Exit the current sub-state first
                std::visit([](const auto &s) { s.onExit(); }, subState);
                std::cout << "  [EXIT] Tracking: Leaving tracking mode\n";
            }

            std::string getSubStateName() const
            {
                return std::visit([](const auto &s) -> std::string { return s.name; }, subState);
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
            static constexpr const char *name = "Operational";
            OperationalSubState          subState;

            Operational() : subState(Initializing{}) {}
            explicit Operational(OperationalSubState sub) : subState(std::move(sub)) {}

            void onEntry() const
            {
                std::cout << "  [ENTRY] Operational: System powered on\n";
                std::visit([](const auto &s) { s.onEntry(); }, subState);
            }

            void onExit() const
            {
                std::visit([](const auto &s) { s.onExit(); }, subState);
                std::cout << "  [EXIT] Operational: Shutting down systems\n";
            }

            std::string getSubStateName() const
            {
                return std::visit(
                    [](const auto &s) -> std::string
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
    // HSM - Hierarchical State Machine
    // ============================================================================

    /**
     * @brief Hierarchical State Machine for Laser Tracker
     *
     * Uses std::variant for type-safe state representation and std::visit
     * for event dispatching with proper entry/exit action handling.
     */
    class HSM
    {
      public:
        HSM() : currentState_(States::Off{})
        {
            std::cout << "=== Laser Tracker HSM Initialized ===\n";
            std::visit([](const auto &s) { s.onEntry(); }, currentState_);
        }

        /**
         * @brief Process an event and perform state transition if applicable
         * @param event The event to process
         * @return true if a transition occurred, false otherwise
         */
        bool processEvent(const Event &event)
        {
            std::cout << "\n>>> Event: " << getEventName(event) << "\n";

            bool transitioned = std::visit([this, &event](auto &state) -> bool { return this->handleEvent(state, event); }, currentState_);

            if (!transitioned)
            {
                std::cout << "  (Event ignored in current state)\n";
            }

            return transitioned;
        }

        /**
         * @brief Get the current state name (including hierarchy)
         */
        std::string getCurrentStateName() const
        {
            return std::visit(
                [](const auto &s) -> std::string
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
        const State &getState() const { return currentState_; }
        State       &getState() { return currentState_; }

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
            std::visit([](auto &s) { s.onExit(); }, currentState_);

            // Enter new state
            currentState_ = std::move(newState);
            std::visit([](auto &s) { s.onEntry(); }, currentState_);
        }

        // Transition within Operational composite state
        template <typename NewSubState> void transitionOperationalTo(States::Operational &op, NewSubState newSub)
        {
            // Exit current sub-state
            std::visit([](auto &s) { s.onExit(); }, op.subState);

            // Enter new sub-state
            op.subState = std::move(newSub);
            std::visit([](auto &s) { s.onEntry(); }, op.subState);
        }

        // Transition within Tracking composite state
        template <typename NewSubState> void transitionTrackingTo(States::Tracking &tracking, NewSubState newSub)
        {
            // Exit current sub-state
            std::visit([](auto &s) { s.onExit(); }, tracking.subState);

            // Enter new sub-state
            tracking.subState = std::move(newSub);
            std::visit([](auto &s) { s.onEntry(); }, tracking.subState);
        }

        // ------------------------------------------------------------------------
        // Event handlers for each state
        // ------------------------------------------------------------------------

        // Handle events in Off state
        bool handleEvent(States::Off &state, const Event &event)
        {
            return std::visit(
                [this, &state](const auto &e) -> bool
                {
                    using E = std::decay_t<decltype(e)>;

                    if constexpr (std::is_same_v<E, Events::PowerOn>)
                    {
                        transitionTo(States::Operational{});
                        return true;
                    }
                    return false;
                },
                event);
        }

        // Handle events in Operational state (with sub-state dispatch)
        bool handleEvent(States::Operational &state, const Event &event)
        {
            // First, check for events handled at Operational level
            bool handled = std::visit(
                [this, &state](const auto &e) -> bool
                {
                    using E = std::decay_t<decltype(e)>;

                    if constexpr (std::is_same_v<E, Events::PowerOff>)
                    {
                        state.onExit(); // Exit Operational (which exits sub-state)
                        currentState_ = States::Off{};
                        std::get<States::Off>(currentState_).onEntry();
                        return true;
                    }
                    return false;
                },
                event);

            if (handled)
                return true;

            // Dispatch to sub-state handler
            return std::visit([this, &state, &event](auto &subState) -> bool { return this->handleOperationalSubEvent(state, subState, event); },
                              state.subState);
        }

        // Handle events in Initializing sub-state
        bool handleOperationalSubEvent(States::Operational &parent, States::Initializing &state, const Event &event)
        {
            return std::visit(
                [this, &parent, &state](const auto &e) -> bool
                {
                    using E = std::decay_t<decltype(e)>;

                    if constexpr (std::is_same_v<E, Events::InitComplete>)
                    {
                        transitionOperationalTo(parent, States::Idle{});
                        return true;
                    }
                    else if constexpr (std::is_same_v<E, Events::InitFailed>)
                    {
                        transitionOperationalTo(parent, States::Error{-1, e.errorReason});
                        return true;
                    }
                    return false;
                },
                event);
        }

        // Handle events in Idle sub-state
        bool handleOperationalSubEvent(States::Operational &parent, States::Idle & /*state*/, const Event &event)
        {
            return std::visit(
                [this, &parent](const auto &e) -> bool
                {
                    using E = std::decay_t<decltype(e)>;

                    if constexpr (std::is_same_v<E, Events::StartSearch>)
                    {
                        transitionOperationalTo(parent, States::Tracking{});
                        return true;
                    }
                    else if constexpr (std::is_same_v<E, Events::ErrorOccurred>)
                    {
                        transitionOperationalTo(parent, States::Error{e.errorCode, e.description});
                        return true;
                    }
                    return false;
                },
                event);
        }

        // Handle events in Tracking sub-state (with its own sub-states)
        bool handleOperationalSubEvent(States::Operational &parent, States::Tracking &state, const Event &event)
        {
            // First check for events handled at Tracking level
            bool handled = std::visit(
                [this, &parent, &state](const auto &e) -> bool
                {
                    using E = std::decay_t<decltype(e)>;

                    if constexpr (std::is_same_v<E, Events::ReturnToIdle>)
                    {
                        transitionOperationalTo(parent, States::Idle{});
                        return true;
                    }
                    else if constexpr (std::is_same_v<E, Events::ErrorOccurred>)
                    {
                        // Exit Tracking (which exits its sub-state)
                        state.onExit();
                        parent.subState = States::Error{e.errorCode, e.description};
                        std::get<States::Error>(parent.subState).onEntry();
                        return true;
                    }
                    return false;
                },
                event);

            if (handled)
                return true;

            // Dispatch to Tracking sub-state handler
            return std::visit([this, &parent, &state, &event](auto &subState) -> bool { return this->handleTrackingSubEvent(parent, state, subState, event); },
                              state.subState);
        }

        // Handle events in Error sub-state
        bool handleOperationalSubEvent(States::Operational &parent, States::Error & /*state*/, const Event &event)
        {
            return std::visit(
                [this, &parent](const auto &e) -> bool
                {
                    using E = std::decay_t<decltype(e)>;

                    if constexpr (std::is_same_v<E, Events::Reset>)
                    {
                        // Reset goes back to Initializing
                        transitionOperationalTo(parent, States::Initializing{});
                        return true;
                    }
                    return false;
                },
                event);
        }

        // Handle events in Searching sub-state
        bool handleTrackingSubEvent(States::Operational & /*parent*/, States::Tracking &tracking, States::Searching & /*state*/, const Event &event)
        {
            return std::visit(
                [this, &tracking](const auto &e) -> bool
                {
                    using E = std::decay_t<decltype(e)>;

                    if constexpr (std::is_same_v<E, Events::TargetFound>)
                    {
                        transitionTrackingTo(tracking, States::Locked{e.distance_mm});
                        return true;
                    }
                    return false;
                },
                event);
        }

        // Handle events in Locked sub-state
        bool handleTrackingSubEvent(States::Operational & /*parent*/, States::Tracking &tracking, States::Locked & /*state*/, const Event &event)
        {
            return std::visit(
                [this, &tracking](const auto &e) -> bool
                {
                    using E = std::decay_t<decltype(e)>;

                    if constexpr (std::is_same_v<E, Events::StartMeasure>)
                    {
                        transitionTrackingTo(tracking, States::Measuring{});
                        return true;
                    }
                    else if constexpr (std::is_same_v<E, Events::TargetLost>)
                    {
                        transitionTrackingTo(tracking, States::Searching{});
                        return true;
                    }
                    return false;
                },
                event);
        }

        // Handle events in Measuring sub-state
        bool handleTrackingSubEvent(States::Operational & /*parent*/, States::Tracking &tracking, States::Measuring &state, const Event &event)
        {
            return std::visit(
                [this, &tracking, &state](const auto &e) -> bool
                {
                    using E = std::decay_t<decltype(e)>;

                    if constexpr (std::is_same_v<E, Events::MeasurementComplete>)
                    {
                        state.recordMeasurement(e.x, e.y, e.z);
                        return true;
                    }
                    else if constexpr (std::is_same_v<E, Events::StopMeasure>)
                    {
                        transitionTrackingTo(tracking, States::Locked{});
                        return true;
                    }
                    else if constexpr (std::is_same_v<E, Events::TargetLost>)
                    {
                        transitionTrackingTo(tracking, States::Searching{});
                        return true;
                    }
                    return false;
                },
                event);
        }
    };

} // namespace LaserTracker
