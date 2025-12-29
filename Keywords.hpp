#pragma once

/**
 * @file Keywords.hpp
 * @brief Compile-time string constants for JSON keys and field names
 *
 * This file defines all string literals used as JSON keys throughout the HSM.
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

    } // namespace Keys
} // namespace LaserTracker
