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
// PowerOn/PowerOff Tests (Commands)
// ============================================================================

TEST_F(HSMTest, PowerOnTransitionsToOperationalInitializing)
{
    bool handled = hsm.processMessage(Commands::PowerOn{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Initializing");
    EXPECT_TRUE(hsm.isInState<States::Operational>());
}

TEST_F(HSMTest, PowerOffFromOperationalTransitionsToOff)
{
    hsm.processMessage(Commands::PowerOn{});
    bool handled = hsm.processMessage(Commands::PowerOff{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Off");
    EXPECT_TRUE(hsm.isInState<States::Off>());
}

TEST_F(HSMTest, PowerOnIgnoredWhenAlreadyOperational)
{
    hsm.processMessage(Commands::PowerOn{});
    bool handled = hsm.processMessage(Commands::PowerOn{});
    EXPECT_FALSE(handled);
}

TEST_F(HSMTest, PowerOffIgnoredWhenAlreadyOff)
{
    bool handled = hsm.processMessage(Commands::PowerOff{});
    EXPECT_FALSE(handled);
}

// ============================================================================
// Initialization Tests (Events)
// ============================================================================

TEST_F(HSMTest, InitCompleteTransitionsToIdle)
{
    hsm.processMessage(Commands::PowerOn{});
    bool handled = hsm.processMessage(Events::InitComplete{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Idle");
}

TEST_F(HSMTest, InitFailedTransitionsToError)
{
    hsm.processMessage(Commands::PowerOn{});
    bool handled = hsm.processMessage(Events::InitFailed{"Sensor failure"});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Error");
}

TEST_F(HSMTest, InitCompleteIgnoredInOff)
{
    bool handled = hsm.processMessage(Events::InitComplete{});
    EXPECT_FALSE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Off");
}

// ============================================================================
// Tracking State Tests
// ============================================================================

TEST_F(HSMTest, StartSearchTransitionsToTracking)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    bool handled = hsm.processMessage(Commands::StartSearch{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Searching");
}

TEST_F(HSMTest, TargetFoundTransitionsToLocked)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    hsm.processMessage(Commands::StartSearch{});
    bool handled = hsm.processMessage(Events::TargetFound{5000.0});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Locked");
}

TEST_F(HSMTest, StartMeasureTransitionsToMeasuring)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    hsm.processMessage(Commands::StartSearch{});
    hsm.processMessage(Events::TargetFound{5000.0});
    bool handled = hsm.processMessage(Commands::StartMeasure{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Measuring");
}

TEST_F(HSMTest, MeasurementCompleteStaysInMeasuring)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    hsm.processMessage(Commands::StartSearch{});
    hsm.processMessage(Events::TargetFound{5000.0});
    hsm.processMessage(Commands::StartMeasure{});
    bool handled = hsm.processMessage(Events::MeasurementComplete{1.0, 2.0, 3.0});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Measuring");
}

TEST_F(HSMTest, StopMeasureTransitionsBackToLocked)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    hsm.processMessage(Commands::StartSearch{});
    hsm.processMessage(Events::TargetFound{5000.0});
    hsm.processMessage(Commands::StartMeasure{});
    bool handled = hsm.processMessage(Commands::StopMeasure{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Locked");
}

// ============================================================================
// Target Loss Tests (Events)
// ============================================================================

TEST_F(HSMTest, TargetLostFromLockedTransitionsToSearching)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    hsm.processMessage(Commands::StartSearch{});
    hsm.processMessage(Events::TargetFound{5000.0});
    bool handled = hsm.processMessage(Events::TargetLost{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Searching");
}

TEST_F(HSMTest, TargetLostFromMeasuringTransitionsToSearching)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    hsm.processMessage(Commands::StartSearch{});
    hsm.processMessage(Events::TargetFound{5000.0});
    hsm.processMessage(Commands::StartMeasure{});
    bool handled = hsm.processMessage(Events::TargetLost{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Searching");
}

// ============================================================================
// Return to Idle Tests (Commands)
// ============================================================================

TEST_F(HSMTest, ReturnToIdleFromTrackingTransitionsToIdle)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    hsm.processMessage(Commands::StartSearch{});
    bool handled = hsm.processMessage(Commands::ReturnToIdle{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Idle");
}

TEST_F(HSMTest, ReturnToIdleFromLockedTransitionsToIdle)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    hsm.processMessage(Commands::StartSearch{});
    hsm.processMessage(Events::TargetFound{5000.0});
    bool handled = hsm.processMessage(Commands::ReturnToIdle{});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Idle");
}

// ============================================================================
// Error Handling Tests (Events and Commands)
// ============================================================================

TEST_F(HSMTest, ErrorOccurredFromIdleTransitionsToError)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    bool handled = hsm.processMessage(Events::ErrorOccurred{100, "Test error"});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Error");
}

TEST_F(HSMTest, ErrorOccurredFromTrackingTransitionsToError)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    hsm.processMessage(Commands::StartSearch{});
    bool handled = hsm.processMessage(Events::ErrorOccurred{101, "Tracking error"});
    EXPECT_TRUE(handled);
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Error");
}

TEST_F(HSMTest, ResetFromErrorTransitionsToInitializing)
{
    hsm.processMessage(Commands::PowerOn{});
    hsm.processMessage(Events::InitComplete{});
    hsm.processMessage(Events::ErrorOccurred{100, "Test error"});
    bool handled = hsm.processMessage(Commands::Reset{});
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

    // Power on -> Initializing (Command)
    EXPECT_TRUE(hsm.processMessage(Commands::PowerOn{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Initializing");

    // Init complete -> Idle (Event)
    EXPECT_TRUE(hsm.processMessage(Events::InitComplete{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Idle");

    // Start search -> Searching (Command)
    EXPECT_TRUE(hsm.processMessage(Commands::StartSearch{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Searching");

    // Target found -> Locked (Event)
    EXPECT_TRUE(hsm.processMessage(Events::TargetFound{5000.0}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Locked");

    // Start measure -> Measuring (Command)
    EXPECT_TRUE(hsm.processMessage(Commands::StartMeasure{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Measuring");

    // Record measurements (Events)
    EXPECT_TRUE(hsm.processMessage(Events::MeasurementComplete{1.0, 2.0, 3.0}));
    EXPECT_TRUE(hsm.processMessage(Events::MeasurementComplete{4.0, 5.0, 6.0}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Measuring");

    // Stop measure -> Locked (Command)
    EXPECT_TRUE(hsm.processMessage(Commands::StopMeasure{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Tracking::Locked");

    // Return to idle (Command)
    EXPECT_TRUE(hsm.processMessage(Commands::ReturnToIdle{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Operational::Idle");

    // Power off -> Off (Command)
    EXPECT_TRUE(hsm.processMessage(Commands::PowerOff{}));
    EXPECT_EQ(hsm.getCurrentStateName(), "Off");
}
