/**
 * @file src/jochona/host_volume.h
 * @brief Host output-volume read/write backing the `host.volume.read`/
 *        `host.volume.write` canonical permissions and the manifest's
 *        `runtimeControls.hostVolume` field.
 *
 * Windows-only for now (WASAPI IAudioEndpointVolume on the default render
 * endpoint), matching the ADR's "Windows 11/NVIDIA qualifies first"
 * sequencing. Every other platform honestly reports the control as
 * unavailable rather than fabricating a range.
 */
#pragma once

// standard includes
#include <optional>
#include <string>

// lib includes
#include <nlohmann/json.hpp>

namespace jochona::host_volume {

  /**
   * @brief Current host-volume control status.
   */
  struct status_t {
    bool available = false;  ///< True if a real volume endpoint was found on this platform.
    int min = 0;  ///< Minimum volume, always 0 when available.
    int max = 0;  ///< Maximum volume, always 100 when available.
    int current = 0;  ///< Current volume on the 0-100 scale, valid when available.
  };

  /**
   * @brief Query the current host output volume.
   *
   * @return The current host-volume control status.
   */
  [[nodiscard]] status_t get();

  /**
   * @brief Set the host output volume.
   *
   * @param level_0_100 Desired volume, clamped to [0, 100] before applying.
   * @return True on success; false when unavailable on this platform or the
   *         platform call failed.
   */
  bool set(int level_0_100);

  /**
   * @brief Serialize a host-volume status to the exact JSON body returned
   *        by `GET`/`PUT /jochona/v1/volume`.
   *
   * `{"available": false}` reports `min`/`max`/`current` as `null`, matching
   * the manifest's `runtimeControls.hostVolume` null convention for an
   * unavailable control.
   *
   * @param status Host-volume status to serialize.
   * @return The JSON body for `GET`/`PUT /jochona/v1/volume`.
   */
  [[nodiscard]] nlohmann::json to_json(const status_t &status);

  /**
   * @brief Structured rejection body for `PUT /jochona/v1/volume`.
   */
  struct rejection_t {
    std::string error;  ///< "invalid_parameter" | "host_volume_unavailable".
    std::string detail;  ///< Human-readable explanation.
  };

  /**
   * @brief Serialize a rejection_t to the exact JSON body Jochona Client
   *        expects on a non-2xx `/jochona/v1/volume` response.
   *
   * @param rejection Rejection to serialize.
   * @return The JSON body for the non-2xx `/jochona/v1/volume` response.
   */
  [[nodiscard]] std::string to_json(const rejection_t &rejection);

}  // namespace jochona::host_volume
