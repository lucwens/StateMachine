/**
 * @file test_action_commands.cpp
 * @brief Unit tests for ActionCommands (non-state-changing commands)
 */

#include <gtest/gtest.h>
#include "../ThreadedHSM.hpp"

using namespace LaserTracker;

class ActionCommandTest : public ::testing::Test
{
  protected:
    ThreadedHSM hsm;

    void SetUp() override
    {
        hsm.start();
    }

    void TearDown() override
    {
        hsm.stop();
    }

    void goToIdle()
    {
        hsm.sendStateCommandSync(StateCommands::PowerOn{});
        hsm.sendStateCommandSync(StateCommands::InitComplete{});
    }

    void goToLocked()
    {
        goToIdle();
        hsm.sendStateCommandSync(StateCommands::StartSearch{});
        hsm.sendStateCommandSync(StateCommands::TargetFound{5000.0});
    }

    void goToMeasuring()
    {
        goToLocked();
        hsm.sendStateCommandSync(StateCommands::StartMeasure{});
    }
};

// ============================================================================
// Home Command Tests
// ============================================================================

TEST_F(ActionCommandTest, HomeSucceedsInIdle)
{
    goToIdle();
    auto result = hsm.sendActionCommand(Commands::Home{50.0});
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.params.contains("position"));
}

TEST_F(ActionCommandTest, HomeFailsInOff)
{
    auto result = hsm.sendActionCommand(Commands::Home{50.0});
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(ActionCommandTest, HomeFailsInLocked)
{
    goToLocked();
    auto result = hsm.sendActionCommand(Commands::Home{50.0});
    EXPECT_FALSE(result.success);
}

TEST_F(ActionCommandTest, HomeFailsInMeasuring)
{
    goToMeasuring();
    auto result = hsm.sendActionCommand(Commands::Home{50.0});
    EXPECT_FALSE(result.success);
}

// ============================================================================
// GetPosition Command Tests
// ============================================================================

TEST_F(ActionCommandTest, GetPositionSucceedsInIdle)
{
    goToIdle();
    auto result = hsm.sendActionCommand(Commands::GetPosition{});
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.params.contains("position"));
    EXPECT_TRUE(result.params["position"].contains("x"));
    EXPECT_TRUE(result.params["position"].contains("y"));
    EXPECT_TRUE(result.params["position"].contains("z"));
}

TEST_F(ActionCommandTest, GetPositionSucceedsInLocked)
{
    goToLocked();
    auto result = hsm.sendActionCommand(Commands::GetPosition{});
    EXPECT_TRUE(result.success);
}

TEST_F(ActionCommandTest, GetPositionSucceedsInMeasuring)
{
    goToMeasuring();
    auto result = hsm.sendActionCommand(Commands::GetPosition{});
    EXPECT_TRUE(result.success);
}

TEST_F(ActionCommandTest, GetPositionFailsInOff)
{
    auto result = hsm.sendActionCommand(Commands::GetPosition{});
    EXPECT_FALSE(result.success);
}

// ============================================================================
// SetLaserPower Command Tests
// ============================================================================

TEST_F(ActionCommandTest, SetLaserPowerSucceedsInIdle)
{
    goToIdle();
    Commands::SetLaserPower cmd;
    cmd.powerLevel = 0.75;
    auto result = hsm.sendActionCommand(cmd);
    EXPECT_TRUE(result.success);
}

TEST_F(ActionCommandTest, SetLaserPowerSucceedsInLocked)
{
    goToLocked();
    Commands::SetLaserPower cmd;
    cmd.powerLevel = 0.5;
    auto result = hsm.sendActionCommand(cmd);
    EXPECT_TRUE(result.success);
}

TEST_F(ActionCommandTest, SetLaserPowerFailsInOff)
{
    Commands::SetLaserPower cmd;
    cmd.powerLevel = 0.5;
    auto result = hsm.sendActionCommand(cmd);
    EXPECT_FALSE(result.success);
}

// ============================================================================
// Compensate Command Tests
// ============================================================================

TEST_F(ActionCommandTest, CompensateSucceedsInIdle)
{
    goToIdle();
    Commands::Compensate cmd;
    cmd.temperature = 22.5;
    cmd.pressure = 1015.0;
    cmd.humidity = 45.0;
    auto result = hsm.sendActionCommand(cmd);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.params.contains("compensationFactor"));
    EXPECT_TRUE(result.params.contains("applied"));
}

TEST_F(ActionCommandTest, CompensateSucceedsInLocked)
{
    goToLocked();
    Commands::Compensate cmd;
    cmd.temperature = 20.0;
    cmd.pressure = 1013.25;
    cmd.humidity = 50.0;
    auto result = hsm.sendActionCommand(cmd);
    EXPECT_TRUE(result.success);
}

TEST_F(ActionCommandTest, CompensateFailsInOff)
{
    Commands::Compensate cmd;
    auto result = hsm.sendActionCommand(cmd);
    EXPECT_FALSE(result.success);
}

// ============================================================================
// GetStatus Command Tests
// ============================================================================

TEST_F(ActionCommandTest, GetStatusSucceedsInOff)
{
    auto result = hsm.sendActionCommand(Commands::GetStatus{});
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.params.contains("state"));
    EXPECT_EQ(result.params["state"], "Off");
}

TEST_F(ActionCommandTest, GetStatusSucceedsInIdle)
{
    goToIdle();
    auto result = hsm.sendActionCommand(Commands::GetStatus{});
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.params["state"], "Operational::Idle");
    EXPECT_TRUE(result.params["powered"].get<bool>());
    EXPECT_TRUE(result.params["healthy"].get<bool>());
}

TEST_F(ActionCommandTest, GetStatusSucceedsInTracking)
{
    goToLocked();
    auto result = hsm.sendActionCommand(Commands::GetStatus{});
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.params["state"], "Operational::Tracking::Locked");
}

// ============================================================================
// MoveRelative Command Tests
// ============================================================================

TEST_F(ActionCommandTest, MoveRelativeSucceedsInIdle)
{
    goToIdle();
    Commands::MoveRelative cmd;
    cmd.azimuth = 10.0;
    cmd.elevation = 5.0;
    auto result = hsm.sendActionCommand(cmd);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.params.contains("movedAz"));
    EXPECT_TRUE(result.params.contains("movedEl"));
}

TEST_F(ActionCommandTest, MoveRelativeSucceedsInLocked)
{
    goToLocked();
    Commands::MoveRelative cmd;
    cmd.azimuth = -5.0;
    cmd.elevation = 2.5;
    auto result = hsm.sendActionCommand(cmd);
    EXPECT_TRUE(result.success);
}

TEST_F(ActionCommandTest, MoveRelativeFailsInMeasuring)
{
    goToMeasuring();
    Commands::MoveRelative cmd;
    cmd.azimuth = 1.0;
    cmd.elevation = 1.0;
    auto result = hsm.sendActionCommand(cmd);
    EXPECT_FALSE(result.success);
}

TEST_F(ActionCommandTest, MoveRelativeFailsInOff)
{
    Commands::MoveRelative cmd;
    auto result = hsm.sendActionCommand(cmd);
    EXPECT_FALSE(result.success);
}

// ============================================================================
// Command Properties Tests
// ============================================================================

TEST_F(ActionCommandTest, HomeSyncPropertyIsTrue)
{
    EXPECT_TRUE(Commands::Home::sync);
}

TEST_F(ActionCommandTest, GetPositionSyncPropertyIsFalse)
{
    EXPECT_FALSE(Commands::GetPosition::sync);
}

TEST_F(ActionCommandTest, CompensateSyncPropertyIsTrue)
{
    EXPECT_TRUE(Commands::Compensate::sync);
}

TEST_F(ActionCommandTest, GetStatusSyncPropertyIsFalse)
{
    EXPECT_FALSE(Commands::GetStatus::sync);
}

TEST_F(ActionCommandTest, MoveRelativeSyncPropertyIsTrue)
{
    EXPECT_TRUE(Commands::MoveRelative::sync);
}
