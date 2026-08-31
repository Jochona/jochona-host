/**
 * @file src/jochona/capability_manifest.cpp
 * @brief Definitions for the /jochona/v1/capabilities JSON builder.
 */
#include "capability_manifest.h"

// standard includes
#include <chrono>
#include <format>

// local includes
#include "../httpcommon.h"
#include "../process.h"
#include "../rtsp.h"
#include "display_adapter_client.h"
#include "encoder_tuples.h"
#include "host_volume.h"

namespace jochona::manifest {

  namespace {

    /// Best-effort lookup of the running application's configured name.
    std::string running_application_name(int app_id) {
      for (const auto &app : proc::proc.get_apps()) {
        try {
          if (std::stoll(app.id) == app_id) {
            return app.name;
          }
        } catch (const std::exception &) {
          // Non-numeric app id; not a match for the numeric running() value.
        }
      }
      return {};
    }

    nlohmann::json build_virtual_display(const display_adapter::manifest_status_t &adapter_status, const std::optional<std::vector<display_adapter::slot_status_t>> &slots) {
      nlohmann::json out;
      out["adapter"] = "jochona-windows-display";
      out["version"] = adapter_status.healthy ? std::format("{}.{}.0", adapter_status.protocol_major, adapter_status.protocol_minor) : "0.0.0";
      out["installed"] = adapter_status.installed;
      out["healthy"] = adapter_status.healthy;

      // Real per-slot mode/state from ENUMERATE_SLOTS -- protocol v1.0 has
      // exactly one stable slot (JOCHONA_PROTOCOL_V1_MAX_SLOTS), addressed
      // here as "default" since there is no admin-configurable named-pool
      // feature yet. When enumeration itself fails (driver present but the
      // IOCTL didn't answer), the pool is reported empty rather than
      // publishing a guessed width/height/fps/hdr shape.
      nlohmann::json pool = nlohmann::json::array();
      if (slots) {
        for (const auto &slot : *slots) {
          nlohmann::json entry;
          entry["id"] = "default";
          entry["width"] = slot.mode.width;
          entry["height"] = slot.mode.height;
          entry["fps"] = slot.mode.refresh_denominator > 0 ? slot.mode.refresh_numerator / slot.mode.refresh_denominator : slot.mode.refresh_numerator;
          entry["hdr"] = slot.mode.hdr_enabled;
          entry["state"] = slot.leased ? "in-use" : "available";
          pool.push_back(entry);
        }
      }
      out["pool"] = pool;
      return out;
    }

  }  // namespace

  nlohmann::json build_encoder_tuple(const encoder::proven_tuple_t &tuple) {
    nlohmann::json out;
    out["id"] = tuple.id;
    out["codec"] = tuple.key.codec;
    out["profile"] = tuple.key.profile;
    out["bitDepth"] = tuple.key.profile == "main10" ? 10 : 8;
    out["chroma"] = tuple.key.chroma;
    out["width"] = tuple.key.width;
    out["height"] = tuple.key.height;
    out["fps"] = tuple.key.fps;
    out["hdr"]["supported"] = tuple.key.hdr;
    out["hdr"]["metadata"] = tuple.key.hdr ? nlohmann::json::array({"mastering-display", "max-cll", "max-fall"}) : nlohmann::json::array();
    out["capture"] = tuple.captures;
    out["proof"]["method"] = tuple.method;
    out["proof"]["gpu"] = tuple.environment.gpu;
    out["proof"]["driver"] = tuple.environment.driver;
    out["proof"]["displayMode"] = tuple.environment.display_mode;
    out["proof"]["virtualDisplayAdapter"] = tuple.environment.virtual_display_adapter_version;
    out["proof"]["hostBuild"] = tuple.environment.host_build;
    out["proof"]["verifiedAt"] = std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(tuple.verified_at));
    return out;
  }

  nlohmann::json build(const capability::permission_set_t &permissions) {
    nlohmann::json root;

    root["schema"]["major"] = 1;
    root["schema"]["minor"] = 0;

    auto current_app_id = proc::proc.running();
    const bool busy = current_app_id > 0 || rtsp_stream::session_count() > 0;
    root["host"]["software"] = "Jochona Host";
    root["host"]["build"] = PROJECT_VERSION;
    root["host"]["platform"] = SUNSHINE_PLATFORM;
    root["host"]["identity"] = http::unique_id;
    root["host"]["capacity"]["state"] = busy ? "busy" : "ready";
    root["host"]["capacity"]["maxSessions"] = 1;
    if (current_app_id > 0) {
      root["host"]["capacity"]["activeApplication"] = running_application_name(current_app_id);
    } else {
      root["host"]["capacity"]["activeApplication"] = nullptr;
    }

    root["permissions"] = permissions.manifest_strings();

    nlohmann::json tuples = nlohmann::json::array();
    for (const auto &tuple : encoder::store_t::instance().advertised_tuples()) {
      tuples.push_back(build_encoder_tuple(tuple));
    }
    root["encoderTuples"] = tuples;

    root["virtualDisplay"] = build_virtual_display(display_adapter::client_t::instance().probe(), display_adapter::client_t::instance().enumerate_slots());

    root["runtimeControls"]["bitrate"]["available"] = false;
    root["runtimeControls"]["bitrate"]["minKbps"] = nullptr;
    root["runtimeControls"]["bitrate"]["maxKbps"] = nullptr;
    root["runtimeControls"]["bitrate"]["stepKbps"] = nullptr;

    auto volume_status = host_volume::get();
    root["runtimeControls"]["hostVolume"]["available"] = volume_status.available;
    root["runtimeControls"]["hostVolume"]["min"] = volume_status.available ? nlohmann::json(volume_status.min) : nlohmann::json(nullptr);
    root["runtimeControls"]["hostVolume"]["max"] = volume_status.available ? nlohmann::json(volume_status.max) : nlohmann::json(nullptr);

    root["audio"]["channelLayouts"] = nlohmann::json::array({"stereo", "5.1", "7.1"});
    root["audio"]["microphoneInput"] = false;

    return root;
  }

}  // namespace jochona::manifest
