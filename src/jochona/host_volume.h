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

}  // namespace jochona::host_volume
