/**
 * @file tests/unit/platform/windows/test_vhf_gamepad_policy.cpp
 * @brief Test src/platform/windows/vhf_gamepad_policy.cpp pure translation logic.
 */
#include "../../../tests_common.h"

#ifdef _WIN32
  #include <cstring>
  #include <src/platform/windows/vhf_gamepad_policy.h>

extern "C" {
  #include <moonlight-common-c/src/Limelight.h>
}

/**
 * @brief Test fixture for `platf::vhf_gamepad` translation functions.
 */
class VhfGamepadPolicyTest: public BaseTest {};

using namespace platf::vhf_gamepad;

TEST_F(VhfGamepadPolicyTest, SelectAutomaticBackendPrefersVigem) {
  EXPECT_EQ(select_automatic_backend(true, true), backend_e::vigem);
  EXPECT_EQ(select_automatic_backend(true, false), backend_e::vigem);
}

TEST_F(VhfGamepadPolicyTest, SelectAutomaticBackendFallsBackToVhf) {
  EXPECT_EQ(select_automatic_backend(false, true), backend_e::vhf);
}

TEST_F(VhfGamepadPolicyTest, SelectAutomaticBackendUnavailableWhenNeitherUsable) {
  EXPECT_EQ(select_automatic_backend(false, false), backend_e::unavailable);
}

TEST_F(VhfGamepadPolicyTest, SelectAutomaticProfilePrefersXboxSeries) {
  const auto mask = lvg::profile_bit(lvg::profile::xbox_series) |
                    lvg::profile_bit(lvg::profile::dualsense) |
                    lvg::profile_bit(lvg::profile::dualshock_4);
  const auto selected = select_automatic_profile(mask);
  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(*selected, lvg::profile::xbox_series);
}

TEST_F(VhfGamepadPolicyTest, SelectAutomaticProfilePrefersDualsenseOverDualshock4) {
  const auto mask = lvg::profile_bit(lvg::profile::dualsense) |
                    lvg::profile_bit(lvg::profile::dualshock_4);
  const auto selected = select_automatic_profile(mask);
  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(*selected, lvg::profile::dualsense);
}

TEST_F(VhfGamepadPolicyTest, SelectAutomaticProfileIgnoresReservedGenericProfiles) {
  const auto mask = lvg::profile_bit(lvg::profile::generic_hid) |
                    lvg::profile_bit(lvg::profile::generic_pid) |
                    lvg::profile_bit(lvg::profile::xbox_360);
  EXPECT_FALSE(select_automatic_profile(mask).has_value());
}

TEST_F(VhfGamepadPolicyTest, SelectAutomaticProfileEmptyMaskHasNoValue) {
  EXPECT_FALSE(select_automatic_profile(0).has_value());
}

TEST_F(VhfGamepadPolicyTest, ToMilliUnitsScalesByOneThousand) {
  EXPECT_EQ(to_milli_units(1.0f), 1000);
  EXPECT_EQ(to_milli_units(-2.5f), -2500);
  EXPECT_EQ(to_milli_units(0.0f), 0);
}

TEST_F(VhfGamepadPolicyTest, ToMilliUnitsClampsExtremeValues) {
  EXPECT_EQ(to_milli_units(1.0e12f), 2147483000);
  EXPECT_EQ(to_milli_units(-1.0e12f), -2147483000);
}

TEST_F(VhfGamepadPolicyTest, ToMilliUnitsNanIsZero) {
  EXPECT_EQ(to_milli_units(std::nanf("")), 0);
}

TEST_F(VhfGamepadPolicyTest, ToNormalizedTouchClampsToRange) {
  EXPECT_EQ(to_normalized_touch(-1.0f), 0);
  EXPECT_EQ(to_normalized_touch(0.0f), 0);
  EXPECT_EQ(to_normalized_touch(1.0f), 65535);
  EXPECT_EQ(to_normalized_touch(2.0f), 65535);
}

TEST_F(VhfGamepadPolicyTest, ToNormalizedTouchNanIsZero) {
  EXPECT_EQ(to_normalized_touch(std::nanf("")), 0);
}

TEST_F(VhfGamepadPolicyTest, ToProtocolTouchEventMapsKnownValues) {
  EXPECT_EQ(to_protocol_touch_event(LI_TOUCH_EVENT_DOWN), static_cast<std::uint8_t>(lvg::touch_event::down));
  EXPECT_EQ(to_protocol_touch_event(LI_TOUCH_EVENT_UP), static_cast<std::uint8_t>(lvg::touch_event::up));
  EXPECT_EQ(to_protocol_touch_event(LI_TOUCH_EVENT_MOVE), static_cast<std::uint8_t>(lvg::touch_event::move));
  EXPECT_EQ(to_protocol_touch_event(LI_TOUCH_EVENT_HOVER), static_cast<std::uint8_t>(lvg::touch_event::hover));
  EXPECT_EQ(to_protocol_touch_event(LI_TOUCH_EVENT_CANCEL), static_cast<std::uint8_t>(lvg::touch_event::cancel));
}

TEST_F(VhfGamepadPolicyTest, ToProtocolTouchEventUnmappedBecomesCancelAll) {
  EXPECT_EQ(to_protocol_touch_event(0xFF), static_cast<std::uint8_t>(lvg::touch_event::cancel_all));
}

TEST_F(VhfGamepadPolicyTest, ToProtocolMotionKindMapsKnownValues) {
  EXPECT_EQ(to_protocol_motion_kind(LI_MOTION_TYPE_ACCEL), static_cast<std::uint8_t>(lvg::motion_kind::accelerometer));
  EXPECT_EQ(to_protocol_motion_kind(LI_MOTION_TYPE_GYRO), static_cast<std::uint8_t>(lvg::motion_kind::gyroscope));
}

TEST_F(VhfGamepadPolicyTest, ToProtocolMotionKindUnmappedIsZero) {
  EXPECT_EQ(to_protocol_motion_kind(0xFF), 0);
}

TEST_F(VhfGamepadPolicyTest, ToProtocolBatteryStateMapsKnownValues) {
  EXPECT_EQ(to_protocol_battery_state(LI_BATTERY_STATE_CHARGING), static_cast<std::uint8_t>(lvg::battery_state::charging));
  EXPECT_EQ(to_protocol_battery_state(LI_BATTERY_STATE_DISCHARGING), static_cast<std::uint8_t>(lvg::battery_state::discharging));
  EXPECT_EQ(to_protocol_battery_state(LI_BATTERY_STATE_FULL), static_cast<std::uint8_t>(lvg::battery_state::full));
  EXPECT_EQ(to_protocol_battery_state(LI_BATTERY_STATE_NOT_PRESENT), static_cast<std::uint8_t>(lvg::battery_state::not_present));
  EXPECT_EQ(to_protocol_battery_state(LI_BATTERY_STATE_NOT_CHARGING), static_cast<std::uint8_t>(lvg::battery_state::not_charging));
}

TEST_F(VhfGamepadPolicyTest, ToProtocolBatteryStateUnmappedIsUnknown) {
  EXPECT_EQ(to_protocol_battery_state(0xFF), static_cast<std::uint8_t>(lvg::battery_state::unknown));
}

TEST_F(VhfGamepadPolicyTest, MakeInputStateCopiesFieldsAndFiltersButtons) {
  normalized_state_t state {};
  state.button_flags = supported_button_mask | 0x80000000u;  // extra unsupported bit set
  state.left_trigger = 10;
  state.right_trigger = 20;
  state.left_x = -100;
  state.left_y = 200;
  state.right_x = 300;
  state.right_y = -400;

  const auto request = make_input_state(3, state);
  EXPECT_EQ(request.controller_id, 3u);
  EXPECT_EQ(request.buttons, supported_button_mask);
  EXPECT_EQ(request.left_trigger, 10);
  EXPECT_EQ(request.right_trigger, 20);
  EXPECT_EQ(request.left_x, -100);
  EXPECT_EQ(request.left_y, 200);
  EXPECT_EQ(request.right_x, 300);
  EXPECT_EQ(request.right_y, -400);
  EXPECT_EQ(request.header.version, lvg::k_protocol_version);
  EXPECT_EQ(request.header.size, sizeof(request));
}

TEST_F(VhfGamepadPolicyTest, DecodeRumbleRgbXboxRumbleReport) {
  lvg::feedback_event event {};
  event.type = lvg::feedback_type::xbox_rumble;
  event.payload_size = sizeof(lvg::xbox_rumble_feedback);

  lvg::xbox_rumble_feedback payload {};
  payload.low_frequency = 1000;
  payload.high_frequency = 2000;
  payload.left_trigger = 3000;
  payload.right_trigger = 4000;
  std::memcpy(event.payload, &payload, sizeof(payload));

  rumble_rgb_t feedback {};
  ASSERT_TRUE(decode_rumble_rgb(event, feedback));
  EXPECT_EQ(feedback.low_frequency, 1000);
  EXPECT_EQ(feedback.high_frequency, 2000);
  EXPECT_EQ(feedback.left_trigger, 3000);
  EXPECT_EQ(feedback.right_trigger, 4000);
  EXPECT_TRUE(feedback.has_triggers);
  EXPECT_FALSE(feedback.has_rgb);
}

TEST_F(VhfGamepadPolicyTest, DecodeRumbleRgbPlaystationOutputReport) {
  lvg::feedback_event event {};
  event.type = lvg::feedback_type::playstation_output;
  event.payload_size = sizeof(lvg::playstation_output_feedback);

  lvg::playstation_output_feedback payload {};
  payload.low_frequency = 111;
  payload.high_frequency = 222;
  payload.red = 10;
  payload.green = 20;
  payload.blue = 30;
  payload.valid = lvg::ps_output_lightbar_valid | lvg::ps_output_triggers_valid;
  payload.left_trigger.mode = 0x02;
  payload.right_trigger.mode = 0x06;
  for (int i = 0; i < 10; ++i) {
    payload.left_trigger.parameters[i] = static_cast<std::uint8_t>(i);
    payload.right_trigger.parameters[i] = static_cast<std::uint8_t>(10 + i);
  }
  std::memcpy(event.payload, &payload, sizeof(payload));

  rumble_rgb_t feedback {};
  ASSERT_TRUE(decode_rumble_rgb(event, feedback));
  EXPECT_EQ(feedback.low_frequency, 111);
  EXPECT_EQ(feedback.high_frequency, 222);
  EXPECT_TRUE(feedback.has_rgb);
  EXPECT_EQ(feedback.red, 10);
  EXPECT_EQ(feedback.green, 20);
  EXPECT_EQ(feedback.blue, 30);
  ASSERT_TRUE(feedback.has_trigger_effects);
  EXPECT_EQ(feedback.trigger_event_flags, DS_EFFECT_LEFT_TRIGGER | DS_EFFECT_RIGHT_TRIGGER);
  EXPECT_EQ(feedback.left_effect.mode, 0x02);
  EXPECT_EQ(feedback.right_effect.mode, 0x06);
  EXPECT_EQ(feedback.left_effect.parameters[0], 0);
  EXPECT_EQ(feedback.right_effect.parameters[9], 19);
}

TEST_F(VhfGamepadPolicyTest, DecodeRumbleRgbGenericRumbleHasNoRgb) {
  lvg::feedback_event event {};
  event.type = lvg::feedback_type::generic_rumble;
  event.payload_size = sizeof(lvg::generic_rumble_rgb_feedback);

  lvg::generic_rumble_rgb_feedback payload {};
  payload.low_frequency = 5;
  payload.high_frequency = 6;
  payload.red = 255;  // must be ignored: a force-feedback effect carries no colour
  std::memcpy(event.payload, &payload, sizeof(payload));

  rumble_rgb_t feedback {};
  ASSERT_TRUE(decode_rumble_rgb(event, feedback));
  EXPECT_FALSE(feedback.has_rgb);
  EXPECT_EQ(feedback.red, 0);
}

TEST_F(VhfGamepadPolicyTest, DecodeRumbleRgbRejectsMismatchedPayloadSize) {
  lvg::feedback_event event {};
  event.type = lvg::feedback_type::xbox_rumble;
  event.payload_size = 1;  // wrong size for xbox_rumble_feedback

  rumble_rgb_t feedback {};
  EXPECT_FALSE(decode_rumble_rgb(event, feedback));
}

TEST_F(VhfGamepadPolicyTest, TriggerEffectEqualityComparesModeAndParameters) {
  trigger_effect_t a {};
  trigger_effect_t b {};
  EXPECT_EQ(a, b);

  b.mode = 1;
  EXPECT_NE(a, b);

  b.mode = 0;
  b.parameters[0] = 1;
  EXPECT_NE(a, b);
}

#endif
