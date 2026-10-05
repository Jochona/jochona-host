/**
 * @file src/jochona/capability_manifest.h
 * @brief Builds the JSON body for `GET /jochona/v1/capabilities`, schema 1.0.
 *
 * The shape produced here matches
 * docs/protocols/jochona-host-capabilities.md exactly; see that document for
 * the normative field-by-field description.
 */
#pragma once

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "capabilities.h"
#include "encoder_tuples.h"

namespace jochona::manifest {

  /**
   * @brief Build the full capabilities manifest for the given permission grant.
   *
   * Reads live state only: the currently running application
   * (proc::proc.running()), the proven-tuple store
   * (jochona::encoder::store_t), the display-adapter's actual probed status
   * (jochona::display_adapter::client_t), and the real host-volume control
   * status (jochona::host_volume). Nothing here is a static placeholder.
   *
   * @param permissions Permission grant to scope the manifest to.
   * @return The capabilities manifest JSON body.
   */
  nlohmann::json build(const capability::permission_set_t &permissions);

  /**
   * @brief Serialize one proven encoder tuple to its `encoderTuples[]`
   *        manifest shape (id/codec/profile/bitDepth/chroma/width/height/
   *        fps/hdr/capture/proof).
   *
   * Shared with `POST /jochona/v1/probe`, whose 200 response is exactly one
   * such entry -- the tuple that call just proved and recorded.
   *
   * @param tuple Proven encoder tuple to serialize.
   * @return The `encoderTuples[]` manifest entry for `tuple`.
   */
  nlohmann::json build_encoder_tuple(const encoder::proven_tuple_t &tuple);

}  // namespace jochona::manifest
