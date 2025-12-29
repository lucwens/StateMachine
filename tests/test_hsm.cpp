/**
 * @file test_hsm.cpp
 * @brief Unit tests for HSM state transitions
 */

#include <gtest/gtest.h>
#include "../ThreadedHSM.hpp"

using namespace LaserTracker;

class HSMTest : public ::testing::Test
{
  protected:
    HSM hsm;
};

// ============================================================================
// Initial State Tests
// ============================================================================

TEST_F(HSMTest, InitialStateIsOff)
{
    EXPECT_EQ(hsm.getCurrentStateName(), "Off");
    EXPECT_TRUE(hsm.isInState<States::Off>());
}

// ============================================================================
// PowerOn/PowerOff Tests
// ============================================================================

TEST_F(HSMTest, PowerOnTransitionsToOperationalInitializing)
{
    bool handled = hsm.processCommand(StateCommands::PowerOn{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Initializing");
    EXPECT_TRUE(hsm.isInState<States::Operational>());
}

TEST_F(HSMTest, PowerOffFromOperationalTransitionsToOff)
{
    hsm.processCommand(StateCommands::PowerOn{});
    bool handled = hsm.processCommand(StateCommands::PowerOff{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Off");
    EXPECT_TRUE(hsm.isInState<States::Off>());
}

TEST_F(HSMTest, PowerOnIgnoredWhenAlreadyOperational)
{
    hsm.processCommand(StateCommands::PowerOn{});
    bool handled = hsm.processCommand(StateCommands::PowerOn{});
    EXPECT_FALSE(handled);
}

TEST_F(HSMTest, PowerOffIgnoredWhenAlreadyOff)
{
    bool handled = hsm.processCommand(StateCommands::PowerOff{});
    EXPECT_FALSE(handled);
}

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_F(HSMTest, InitCompleteTransitionsToIdle)
{
    hsm.processCommand(StateCommands::PowerOn{});
    bool handled = hsm.processCommand(StateCommands::InitComplete{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Idle");
}

TEST_F(HSMTest, InitFailedTransitionsToError)
{
    hsm.processCommand(StateCommands::PowerOn{});
    bool handled = hsm.processCommand(StateCommands::InitFailed{"Sensor failure"});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Error");
}

TEST_F(HSMTest, InitCompleteIgnoredInOff)
{
    bool handled = hsm.processCommand(StateCommands::InitComplete{});
    EXPECT_FALSE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Off");
}

// ============================================================================
// Tracking State Tests
// ============================================================================

TEST_F(HSMTest, StartSearchTransitionsToTracking)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    bool handled = hsm.processCommand(StateCommands::StartSearch{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Searching");
}

TEST_F(HSMTest, TargetFoundTransitionsToLocked)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    hsm.processCommand(StateCommands::StartSearch{});
    bool handled = hsm.processCommand(StateCommands::TargetFound{5000.0});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Locked");
}

TEST_F(HSMTest, StartMeasureTransitionsToMeasuring)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    hsm.processCommand(StateCommands::StartSearch{});
    hsm.processCommand(StateCommands::TargetFound{5000.0});
    bool handled = hsm.processCommand(StateCommands::StartMeasure{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Measuring");
}

TEST_F(HSMTest, MeasurementCompleteStaysInMeasuring)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    hsm.processCommand(StateCommands::StartSearch{});
    hsm.processCommand(StateCommands::TargetFound{5000.0});
    hsm.processCommand(StateCommands::StartMeasure{});
    bool handled = hsm.processCommand(StateCommands::MeasurementComplete{1.0, 2.0, 3.0});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Measuring");
}

TEST_F(HSMTest, StopMeasureTransitionsBackToLocked)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    hsm.processCommand(StateCommands::StartSearch{});
    hsm.processCommand(StateCommands::TargetFound{5000.0});
    hsm.processCommand(StateCommands::StartMeasure{});
    bool handled = hsm.processCommand(StateCommands::StopMeasure{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Locked");
}

// ============================================================================
// Target Loss Tests
// ============================================================================

TEST_F(HSMTest, TargetLostFromLockedTransitionsToSearching)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    hsm.processCommand(StateCommands::StartSearch{});
    hsm.processCommand(StateCommands::TargetFound{5000.0});
    bool handled = hsm.processCommand(StateCommands::TargetLost{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Searching");
}

TEST_F(HSMTest, TargetLostFromMeasuringTransitionsToSearching)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    hsm.processCommand(StateCommands::StartSearch{});
    hsm.processCommand(StateCommands::TargetFound{5000.0});
    hsm.processCommand(StateCommands::StartMeasure{});
    bool handled = hsm.processCommand(StateCommands::TargetLost{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Searching");
}

// ============================================================================
// Return to Idle Tests
// ============================================================================

TEST_F(HSMTest, ReturnToIdleFromTrackingTransitionsToIdle)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    hsm.processCommand(StateCommands::StartSearch{});
    bool handled = hsm.processCommand(StateCommands::ReturnToIdle{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Idle");
}

TEST_F(HSMTest, ReturnToIdleFromLockedTransitionsToIdle)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    hsm.processCommand(StateCommands::StartSearch{});
    hsm.processCommand(StateCommands::TargetFound{5000.0});
    bool handled = hsm.processCommand(StateCommands::ReturnToIdle{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Idle");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(HSMTest, ErrorOccurredFromIdleTransitionsToError)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    bool handled = hsm.processCommand(StateCommands::ErrorOccurred{100, "Test error"});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Error");
}

TEST_F(HSMTest, ErrorOccurredFromTrackingTransitionsToError)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    hsm.processCommand(StateCommands::StartSearch{});
    bool handled = hsm.processCommand(StateCommands::ErrorOccurred{101, "Tracking error"});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Error");
}

TEST_F(HSMTest, ResetFromErrorTransitionsToInitializing)
{
    hsm.processCommand(StateCommands::PowerOn{});
    hsm.processCommand(StateCommands::InitComplete{});
    hsm.processCommand(StateCommands::ErrorOccurred{100, "Test error"});
    bool handled = hsm.processCommand(StateCommands::Reset{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Initializing");
}

// ============================================================================
// Complete Workflow Test
// ============================================================================

TEST_F(HSMTest, CompleteWorkflow)
{
    // Start from Off
    EXPECT_EQ(hsm.getCurrentStateName(), "Off");

    // Power on -> Initializing
    EXPECT_TRUE(hsm.processCommand(StateCommands::PowerOn{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Initializing");

    // Init complete -> Idle
    EXPECT_TRUE(hsm.processCommand(StateCommands::InitComplete{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Idle");

    // Start search -> Searching
    EXPECT_TRUE(hsm.processCommand(StateCommands::StartSearch{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Searching");

    // Target found -> Locked
    EXPECT_TRUE(hsm.processCommand(StateCommands::TargetFound{5000.0}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Locked");

    // Start measure -> Measuring
    EXPECT_TRUE(hsm.processCommand(StateCommands::StartMeasure{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Measuring");

    // Record measurements
    EXPECT_TRUE(hsm.processCommand(StateCommands::MeasurementComplete{1.0, 2.0, 3.0}));
    EXPECT_TRUE(hsm.processCommand(StateCommands::MeasurementComplete{4.0, 5.0, 6.0}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Measuring");

    // Stop measure -> Locked
    EXPECT_TRUE(hsm.processCommand(StateCommands::StopMeasure{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Locked");

    // Return to idle
    EXPECT_TRUE(hsm.processCommand(StateCommands::ReturnToIdle{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Idle");

    // Power off -> Off
    EXPECT_TRUE(hsm.processCommand(StateCommands::PowerOff{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Off");
}
