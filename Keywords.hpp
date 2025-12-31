#pragma once

/**
 * @file Keywords.hpp
 * @brief Compile-time string constants for all identifiers in the HSM
 *
 * This file defines all string literals used throughout the HSM:
 * - Keys: JSON field names for serialization
 * - StateNames: State identifiers and hierarchical paths
 * - EventNames: Event message identifiers
 * - CommandNames: Command message identifiers (state-changing and action)
 *
 * Using constexpr ensures:
 * - Compile-time string constants (no runtime allocation)
 * - Single point of definition (easy to rename/refactor)
 * - Type safety (compiler catches typos)
 */

namespace LaserTracker
{
    namespace Keys
    {
        // Position-related keys
        inline constexpr const char* Position  = "position";
        inline constexpr const char* Azimuth   = "azimuth";
        inline constexpr const char* Elevation = "elevation";
        inline constexpr const char* X         = "x";
        inline constexpr const char* Y         = "y";
        inline constexpr const char* Z         = "z";

        // State-related keys
        inline constexpr const char* State        = "state";
        inline constexpr const char* Handled      = "handled";
        inline constexpr const char* StateChanged = "stateChanged";
        inline constexpr const char* Healthy      = "healthy";
        inline constexpr const char* Powered      = "powered";

        // Command parameter keys
        inline constexpr const char* Speed       = "speed";
        inline constexpr const char* PowerLevel  = "powerLevel";
        inline constexpr const char* Temperature = "temperature";
        inline constexpr const char* Pressure    = "pressure";
        inline constexpr const char* Humidity    = "humidity";

        // Event parameter keys
        inline constexpr const char* DistanceMm  = "distance_mm";
        inline constexpr const char* ErrorReason = "errorReason";
        inline constexpr const char* ErrorCode   = "errorCode";
        inline constexpr const char* Description = "description";

        // Result keys
        inline constexpr const char* CompensationFactor = "compensationFactor";
        inline constexpr const char* Applied            = "applied";
        inline constexpr const char* MovedAz            = "movedAz";
        inline constexpr const char* MovedEl            = "movedEl";
        inline constexpr const char* MoveTimeMs         = "moveTimeMs";

        // Message protocol keys
        inline constexpr const char* Id          = "id";
        inline constexpr const char* Name        = "name";
        inline constexpr const char* TimestampMs = "timestamp_ms";
        inline constexpr const char* IsResponse  = "isResponse";
        inline constexpr const char* Success     = "success";
        inline constexpr const char* Result      = "result";
        inline constexpr const char* Error       = "error";
        inline constexpr const char* Params      = "params";
        inline constexpr const char* Sync        = "sync";
        inline constexpr const char* TimeoutMs   = "timeoutMs";
        inline constexpr const char* NeedsReply  = "needsReply";

    } // namespace Keys

    /**
     * @brief State name constants for state checking and expectedState values
     *
     * Leaf state names are used for find()-based state validation in execute() methods.
     * Full paths are used for expectedState matching after state-changing commands.
     */
    namespace StateNames
    {
        // Top-level states
        inline constexpr const char* Off         = "Off";
        inline constexpr const char* Operational = "Operational";

        // Operational sub-states
        inline constexpr const char* Initializing = "Initializing";
        inline constexpr const char* Idle         = "Idle";
        inline constexpr const char* Tracking     = "Tracking";
        inline constexpr const char* Error        = "Error";

        // Tracking sub-states
        inline constexpr const char* Searching = "Searching";
        inline constexpr const char* Locked    = "Locked";
        inline constexpr const char* Measuring = "Measuring";

        // Full hierarchical state paths (for expectedState matching)
        inline constexpr const char* Operational_Idle               = "Operational::Idle";
        inline constexpr const char* Operational_Tracking_Searching = "Operational::Tracking::Searching";
        inline constexpr const char* Operational_Tracking_Locked    = "Operational::Tracking::Locked";
        inline constexpr const char* Operational_Tracking_Measuring = "Operational::Tracking::Measuring";

    } // namespace StateNames

    /**
     * @brief Event name constants (past tense - "what happened")
     *
     * Events represent external occurrences that the state machine reacts to.
     */
    namespace EventNames
    {
        inline constexpr const char* InitComplete        = "InitComplete";
        inline constexpr const char* InitFailed          = "InitFailed";
        inline constexpr const char* TargetFound         = "TargetFound";
        inline constexpr const char* TargetLost          = "TargetLost";
        inline constexpr const char* MeasurementComplete = "MeasurementComplete";
        inline constexpr const char* ErrorOccurred       = "ErrorOccurred";

    } // namespace EventNames

    /**
     * @brief Command name constants (imperative - "what to do")
     *
     * Commands are instructions that drive the state machine.
     * Divided into state-changing commands and action commands.
     */
    namespace CommandNames
    {
        // State-changing commands (trigger state transitions)
        inline constexpr const char* PowerOn      = "PowerOn";
        inline constexpr const char* PowerOff     = "PowerOff";
        inline constexpr const char* StartSearch  = "StartSearch";
        inline constexpr const char* StartMeasure = "StartMeasure";
        inline constexpr const char* StopMeasure  = "StopMeasure";
        inline constexpr const char* Reset        = "Reset";
        inline constexpr const char* ReturnToIdle = "ReturnToIdle";

        // Action commands (execute operations, don't change state)
        inline constexpr const char* Home          = "Home";
        inline constexpr const char* GetPosition   = "GetPosition";
        inline constexpr const char* SetLaserPower = "SetLaserPower";
        inline constexpr const char* Compensate    = "Compensate";
        inline constexpr const char* GetStatus     = "GetStatus";
        inline constexpr const char* MoveRelative  = "MoveRelative";

    } // namespace CommandNames

} // namespace LaserTracker
