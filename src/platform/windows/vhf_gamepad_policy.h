/**
 * @file src/platform/windows/vhf_gamepad_policy.h
 * @brief Declarations for translating normalized gamepad state into the VHF driver protocol.
 */
#pragma once

// standard includes
#include <array>
#include <cstdint>
#include <optional>

// lib includes
#include <libvirtualgamepad/protocol.h>

// local includes
#include "vhf_gamepad_wake_gate.h"

namespace platf::vhf_gamepad {

  /**
   * @brief Backend selected by the `auto` gamepad setting.
   */
  enum class backend_e {
    unavailable,  ///< Neither backend can create controllers.
    vigem,  ///< ViGEmBus is usable.
    vhf  ///< The libvirtualgamepad VHF driver is usable.
  };

  /**
   * @brief Selects the backend used by the Automatic gamepad setting.
   * @details ViGEm remains preferred when usable, matching the existing default. The VHF driver
   *          is the fallback when ViGEmBus is absent or cannot be opened, and is also what makes
   *          a client-reported PS5 controller usable as a real DualSense emulation.
   * @param vigem_available Whether a connection to ViGEmBus succeeded.
   * @param vhf_available Whether the VHF driver exposes a usable controller profile.
   * @return The selected backend, or `unavailable` when neither backend can create controllers.
   */
  [[nodiscard]] constexpr backend_e select_automatic_backend(
    const bool vigem_available,
    const bool vhf_available
  ) noexcept {
    if (vigem_available) {
      return backend_e::vigem;
    }
    if (vhf_available) {
      return backend_e::vhf;
    }
    return backend_e::unavailable;
  }

  /**
   * @brief Normalized controller state, copied field-for-field out of `gamepad_state_t`.
   * @details Keeping this struct free of platform headers lets the translation be tested on its
   *          own.
   */
  struct normalized_state_t {
    std::uint32_t button_flags {};  ///< Moonlight button mask for the current gamepad state.
    std::uint8_t left_trigger {};  ///< Left trigger value.
    std::uint8_t right_trigger {};  ///< Right trigger value.
    std::int16_t left_x {};  ///< Left stick X-axis value.
    std::int16_t left_y {};  ///< Left stick Y-axis value.
    std::int16_t right_x {};  ///< Right stick X-axis value.
    std::int16_t right_y {};  ///< Right stick Y-axis value.
  };

  /**
   * @brief One adaptive trigger's effect program.
   */
  struct trigger_effect_t {
    std::uint8_t mode {};  ///< Adaptive-trigger effect mode.
    std::array<std::uint8_t, 10> parameters {};  ///< Effect parameter block.

    /**
     * @brief Compares two trigger effect programs for equality.
     * @param other The effect to compare against.
     * @return `true` when both the mode and parameters match.
     */
    bool operator==(const trigger_effect_t &other) const noexcept {
      return mode == other.mode && parameters == other.parameters;
    }
  };

  /**
   * @brief A decoded rumble/RGB/adaptive-trigger feedback report from the driver.
   * @details `has_rgb` is false for force-feedback events. A DirectInput effect says nothing
   *          about a light, so forwarding its zeroed colour channels would switch off the LED on
   *          the client's real controller.
   */
  struct rumble_rgb_t {
    std::uint16_t low_frequency {};  ///< Low-frequency rumble motor intensity.
    std::uint16_t high_frequency {};  ///< High-frequency rumble motor intensity.
    std::uint8_t red {};  ///< Lightbar red channel.
    std::uint8_t green {};  ///< Lightbar green channel.
    std::uint8_t blue {};  ///< Lightbar blue channel.
    bool has_rgb {};  ///< Whether `red`/`green`/`blue` are valid.

    // Xbox controllers have impulse triggers. The driver sends all four
    // actuators in one event because it keeps a single pending feedback slot,
    // so splitting them would let coalescing drop one.
    std::uint16_t left_trigger {};  ///< Left impulse-trigger motor intensity.
    std::uint16_t right_trigger {};  ///< Right impulse-trigger motor intensity.
    bool has_triggers {};  ///< Whether `left_trigger`/`right_trigger` are valid.

    // DualSense adaptive trigger programs, forwarded to the client verbatim.
    trigger_effect_t left_effect {};  ///< Left adaptive-trigger effect program.
    trigger_effect_t right_effect {};  ///< Right adaptive-trigger effect program.
    std::uint8_t trigger_event_flags {};  ///< `DS_EFFECT_LEFT_TRIGGER`/`DS_EFFECT_RIGHT_TRIGGER` bits.
    bool has_trigger_effects {};  ///< Whether the adaptive-trigger fields are valid.

    /**
     * @brief Compares two decoded feedback reports for equality.
     * @param other The feedback to compare against.
     * @return `true` when every field matches.
     */
    bool operator==(const rumble_rgb_t &other) const noexcept {
      return low_frequency == other.low_frequency &&
             high_frequency == other.high_frequency &&
             red == other.red && green == other.green && blue == other.blue &&
             has_rgb == other.has_rgb &&
             left_trigger == other.left_trigger &&
             right_trigger == other.right_trigger &&
             has_triggers == other.has_triggers &&
             left_effect == other.left_effect &&
             right_effect == other.right_effect &&
             trigger_event_flags == other.trigger_event_flags &&
             has_trigger_effects == other.has_trigger_effects;
    }
  };

  /**
   * @brief Every button the protocol can carry.
   * @details Bits outside this mask are dropped instead of being sent as unspecified wire state.
   */
  inline constexpr std::uint32_t supported_button_mask =
    lvg::button_mask::dpad_up | lvg::button_mask::dpad_down |
    lvg::button_mask::dpad_left | lvg::button_mask::dpad_right |
    lvg::button_mask::start | lvg::button_mask::back |
    lvg::button_mask::left_stick | lvg::button_mask::right_stick |
    lvg::button_mask::left_shoulder | lvg::button_mask::right_shoulder |
    lvg::button_mask::home |
    lvg::button_mask::south | lvg::button_mask::east |
    lvg::button_mask::west | lvg::button_mask::north |
    lvg::button_mask::paddle_1 | lvg::button_mask::paddle_2 |
    lvg::button_mask::paddle_3 | lvg::button_mask::paddle_4 |
    lvg::button_mask::touchpad | lvg::button_mask::misc;

  /**
   * @brief Selects the preferred public console profile from a driver mask.
   * @details The generic protocol enum values remain reserved, but they have no accepted public
   *          USB PID and must never become an automatic fallback.
   * @param available_profiles The mask returned by the VHF driver.
   * @return The selected console profile, or no value when none is available.
   */
  [[nodiscard]] std::optional<lvg::profile> select_automatic_profile(
    lvg::profile_mask_t available_profiles
  ) noexcept;

  /**
   * @brief Builds a protocol input report for a controller.
   * @param controller_id The driver-side controller slot.
   * @param state The normalized controller state.
   * @return A fully populated request the driver can accept as-is.
   */
  [[nodiscard]] lvg::input_state_request make_input_state(
    std::uint32_t controller_id,
    const normalized_state_t &state
  ) noexcept;

  /**
   * @brief Decodes a driver feedback event into rumble, RGB, and adaptive-trigger values.
   * @details Accepts the rumble/RGB/trigger report a PlayStation output report produces, the
   *          four-motor report an Xbox output report produces, and the rumble-only report a
   *          DirectInput force-feedback effect produces.
   * @param event The event returned by the driver.
   * @param feedback Receives the decoded values when the event carries rumble.
   * @return `true` when `feedback` was populated.
   */
  [[nodiscard]] bool decode_rumble_rgb(const lvg::feedback_event &event, rumble_rgb_t &feedback) noexcept;

  /**
   * @brief Converts a client touch event type into the protocol's.
   * @param event_type The client event type.
   * @return The protocol value, or the cancel-all value for anything unmapped.
   */
  [[nodiscard]] std::uint8_t to_protocol_touch_event(std::uint8_t event_type) noexcept;

  /**
   * @brief Converts a client motion type into the protocol's.
   * @param motion_type The client motion type.
   * @return The protocol value, or 0 when the type has no mapping.
   */
  [[nodiscard]] std::uint8_t to_protocol_motion_kind(std::uint8_t motion_type) noexcept;

  /**
   * @brief Converts a client battery state into the protocol's.
   * @param state The client battery state.
   * @return The protocol value.
   */
  [[nodiscard]] std::uint8_t to_protocol_battery_state(std::uint8_t state) noexcept;

  /**
   * @brief Normalizes a 0..1 touch coordinate to the protocol's 0..65535.
   * @param value The client value.
   * @return The normalized value, clamped.
   */
  [[nodiscard]] std::uint16_t to_normalized_touch(float value) noexcept;

  /**
   * @brief Converts a motion sample to the protocol's signed milli-units.
   * @param value Acceleration in m/s^2 or angular velocity in degrees/second.
   * @return The value scaled by 1000 and clamped to the protocol's range.
   */
  [[nodiscard]] std::int32_t to_milli_units(float value) noexcept;

}  // namespace platf::vhf_gamepad
