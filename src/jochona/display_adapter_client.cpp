/**
 * @file src/jochona/display_adapter_client.cpp
 * @brief Definitions for the Host-side Jochona Display Adapter IOCTL client.
 */
#include "display_adapter_client.h"

// standard includes
#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
  // platform includes
  #include <setupapi.h>
  #include <windows.h>

  // local includes -- verbatim copies of the canonical ABI owned by the
  // jochona-display-adapter repository; see provenance headers in each file.
  #include "display_adapter_abi.h"
  #include "display_adapter_guid.h"
#endif

namespace jochona::display_adapter {

  std::string_view to_string(error_e error) {
    switch (error) {
      case error_e::driver_not_installed:
        return "driver_not_installed";
      case error_e::protocol_version_mismatch:
        return "protocol_version_mismatch";
      case error_e::slot_not_found:
        return "slot_not_found";
      case error_e::slot_busy:
        return "slot_busy";
      case error_e::slot_not_leased:
        return "slot_not_leased";
      case error_e::lease_token_mismatch:
        return "lease_token_mismatch";
      case error_e::invalid_parameter:
        return "invalid_parameter";
      case error_e::io_failure:
        return "io_failure";
    }
    return "io_failure";
  }

#ifdef _WIN32

  namespace {

    /**
     * @brief Resolve the device path for the Jochona Display Adapter's
     *        registered device interface, or an empty string if no such
     *        device is currently present.
     */
    std::wstring find_device_path() {
      HDEVINFO dev_info = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_JOCHONA_DISPLAY_ADAPTER, nullptr, nullptr, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
      if (dev_info == INVALID_HANDLE_VALUE) {
        return {};
      }

      SP_DEVICE_INTERFACE_DATA iface_data {};
      iface_data.cbSize = sizeof(iface_data);
      if (!SetupDiEnumDeviceInterfaces(dev_info, nullptr, &GUID_DEVINTERFACE_JOCHONA_DISPLAY_ADAPTER, 0, &iface_data)) {
        SetupDiDestroyDeviceInfoList(dev_info);
        return {};
      }

      DWORD required = 0;
      SetupDiGetDeviceInterfaceDetailW(dev_info, &iface_data, nullptr, 0, &required, nullptr);
      if (required == 0) {
        SetupDiDestroyDeviceInfoList(dev_info);
        return {};
      }

      std::vector<std::uint8_t> buffer(required);
      auto *detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
      detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

      std::wstring path;
      if (SetupDiGetDeviceInterfaceDetailW(dev_info, &iface_data, detail, required, nullptr, nullptr)) {
        path = detail->DevicePath;
      }

      SetupDiDestroyDeviceInfoList(dev_info);
      return path;
    }

    /**
     * @brief Map a Win32 error observed after a failed DeviceIoControl()
     *        call to the corresponding protocol error_e.
     *
     * Mirrors the driver's confirmed NTSTATUS -> Win32 mapping (via
     * RtlNtStatusToDosError at the WDF boundary, documented in the
     * jochona-display-adapter repository's docs/PROTOCOL.md):
     * STATUS_INVALID_DEVICE_REQUEST/STATUS_BUFFER_TOO_SMALL -> io_failure
     * (protocol-shape bugs, not caller-actionable), STATUS_REVISION_MISMATCH
     * -> protocol_version_mismatch, STATUS_NOT_FOUND -> slot_not_found,
     * STATUS_DEVICE_BUSY -> slot_busy, STATUS_INVALID_DEVICE_STATE ->
     * slot_not_leased, STATUS_ACCESS_DENIED -> lease_token_mismatch,
     * STATUS_INVALID_PARAMETER -> invalid_parameter.
     */
    error_e map_last_error() {
      switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_DEV_NOT_EXIST:
          return error_e::driver_not_installed;
        case ERROR_REVISION_MISMATCH:
          return error_e::protocol_version_mismatch;
        case ERROR_NOT_FOUND:
          return error_e::slot_not_found;
        case ERROR_BUSY:
          return error_e::slot_busy;
        case ERROR_INVALID_STATE:
          return error_e::slot_not_leased;
        case ERROR_ACCESS_DENIED:
          return error_e::lease_token_mismatch;
        case ERROR_INVALID_PARAMETER:
          return error_e::invalid_parameter;
        default:
          return error_e::io_failure;
      }
    }

    /// True for Win32 errors that mean the device handle itself is no longer usable.
    bool is_handle_level_failure(DWORD win32_error) {
      switch (win32_error) {
        case ERROR_INVALID_HANDLE:
        case ERROR_FILE_NOT_FOUND:
        case ERROR_GEN_FAILURE:
        case ERROR_DEVICE_NOT_CONNECTED:
        case ERROR_DEV_NOT_EXIST:
          return true;
        default:
          return false;
      }
    }

    JochonaProtocolVersion current_protocol_version() {
      return {JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MAJOR, JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MINOR};
    }

  }  // namespace

  struct client_t::impl_t {
    std::mutex mutex;  ///< Serializes access to `device`.
    HANDLE device = INVALID_HANDLE_VALUE;  ///< Lazily opened, cached device handle.

    /// Send an IOCTL on the cached handle; closes+resets the handle on a
    /// handle-level failure so the next call reopens it. Caller holds `mutex`.
    BOOL send(DWORD code, const void *in, DWORD in_size, void *out, DWORD out_size, DWORD &bytes_returned) {
      BOOL ok = DeviceIoControl(device, code, const_cast<void *>(in), in_size, out, out_size, &bytes_returned, nullptr);
      if (!ok && is_handle_level_failure(GetLastError())) {
        CloseHandle(device);
        device = INVALID_HANDLE_VALUE;
      }
      return ok;
    }

    ~impl_t() {
      if (device != INVALID_HANDLE_VALUE) {
        CloseHandle(device);
      }
    }
  };

  struct lease_handle_t::impl_t {
    std::shared_ptr<client_t::impl_t> client_impl;
    JochonaGuid128 lease_token {};
    std::uint32_t slot_id = 0;
    std::uint32_t watchdog_timeout_ms = 5000;
    std::atomic<bool> stop_flag {false};
    std::atomic<bool> released {false};
    std::thread watchdog_thread;

    void ping() {
      JochonaWatchdogPingIn in {};
      in.RequestedVersion = current_protocol_version();
      in.SlotId = slot_id;
      in.LeaseToken = lease_token;

      std::lock_guard lock {client_impl->mutex};
      if (client_impl->device == INVALID_HANDLE_VALUE) {
        return;
      }
      DWORD bytes = 0;
      // Best-effort: a missed ping just means the driver's own watchdog
      // timeout will eventually reclaim the slot; the next configure() or
      // release() call surfaces any real error to the caller.
      client_impl->send(IOCTL_JOCHONA_WATCHDOG_PING, &in, sizeof(in), nullptr, 0, bytes);
    }

    void start_watchdog() {
      auto interval = std::chrono::milliseconds(std::max<std::uint32_t>(500, watchdog_timeout_ms / 2));
      watchdog_thread = std::thread([this, interval] {
        while (!stop_flag.load(std::memory_order_acquire)) {
          std::this_thread::sleep_for(interval);
          if (stop_flag.load(std::memory_order_acquire)) {
            break;
          }
          ping();
        }
      });
    }

    ~impl_t() {
      stop_flag.store(true, std::memory_order_release);
      if (watchdog_thread.joinable()) {
        watchdog_thread.join();
      }
    }
  };

  client_t::client_t():
      impl_(std::make_shared<impl_t>()) {
  }

  client_t &client_t::instance() {
    static client_t instance;
    return instance;
  }

  bool client_t::ensure_open() {
    std::lock_guard lock {impl_->mutex};
    if (impl_->device != INVALID_HANDLE_VALUE) {
      return true;
    }
    auto path = find_device_path();
    if (path.empty()) {
      return false;
    }
    impl_->device = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return impl_->device != INVALID_HANDLE_VALUE;
  }

  manifest_status_t client_t::probe() {
    manifest_status_t status;

    auto path = find_device_path();
    if (path.empty()) {
      status.detail = "No Jochona Display Adapter device interface was found.";
      return status;
    }
    status.installed = true;

    HANDLE probe_handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe_handle == INVALID_HANDLE_VALUE) {
      status.detail = std::format("Found the adapter device but could not open it (Win32 error {}).", GetLastError());
      return status;
    }

    JochonaGetProtocolVersionOut out {};
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(probe_handle, IOCTL_JOCHONA_GET_PROTOCOL_VERSION, nullptr, 0, &out, sizeof(out), &bytes, nullptr);
    CloseHandle(probe_handle);

    if (!ok || bytes != sizeof(out)) {
      status.detail = std::format("GET_PROTOCOL_VERSION failed (Win32 error {}).", GetLastError());
      return status;
    }

    status.protocol_major = out.Version.Major;
    status.protocol_minor = out.Version.Minor;

    if (out.Version.Major != JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MAJOR) {
      status.detail = std::format(
        "Driver protocol version {}.{} is incompatible with Host's {}.{}.",
        out.Version.Major,
        out.Version.Minor,
        JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MAJOR,
        JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MINOR
      );
      return status;
    }

    status.healthy = true;
    status.detail = "Driver installed and protocol-compatible.";
    return status;
  }

  std::variant<lease_handle_t, error_e> client_t::lease(const std::array<std::uint8_t, 16> &owner_id) {
    if (!ensure_open()) {
      return error_e::driver_not_installed;
    }

    JochonaLeaseSlotIn in {};
    in.RequestedVersion = current_protocol_version();
    in.SlotId = 0;
    std::memcpy(in.OwnerId.Bytes, owner_id.data(), owner_id.size());

    JochonaLeaseSlotOut out {};
    DWORD bytes = 0;
    {
      std::lock_guard lock {impl_->mutex};
      if (!impl_->send(IOCTL_JOCHONA_LEASE_SLOT, &in, sizeof(in), &out, sizeof(out), bytes) || bytes != sizeof(out)) {
        return map_last_error();
      }
    }

    // Discover the real watchdog timeout so the ping interval tracks the
    // driver's actual configuration instead of a hardcoded guess.
    std::uint32_t timeout_ms = 5000;
    {
      JochonaGetWatchdogIn watchdog_in {};
      watchdog_in.RequestedVersion = current_protocol_version();
      watchdog_in.SlotId = 0;
      JochonaGetWatchdogOut watchdog_out {};
      DWORD watchdog_bytes = 0;
      std::lock_guard lock {impl_->mutex};
      if (impl_->send(IOCTL_JOCHONA_GET_WATCHDOG, &watchdog_in, sizeof(watchdog_in), &watchdog_out, sizeof(watchdog_out), watchdog_bytes) && watchdog_bytes == sizeof(watchdog_out)) {
        timeout_ms = watchdog_out.TimeoutMilliseconds;
      }
    }

    auto handle_impl = std::make_unique<lease_handle_t::impl_t>();
    handle_impl->client_impl = impl_;
    handle_impl->lease_token = out.LeaseToken;
    handle_impl->slot_id = 0;
    handle_impl->watchdog_timeout_ms = timeout_ms;
    handle_impl->start_watchdog();

    return lease_handle_t {std::move(handle_impl)};
  }

  bool client_t::set_render_adapter_luid(std::uint32_t luid_low_part, std::int32_t luid_high_part) {
    if (!ensure_open()) {
      return false;
    }

    JochonaSetRenderAdapterLuidIn in {};
    in.RequestedVersion = current_protocol_version();
    in.LuidLowPart = static_cast<std::int32_t>(luid_low_part);
    in.LuidHighPart = luid_high_part;

    DWORD bytes = 0;
    std::lock_guard lock {impl_->mutex};
    return impl_->send(IOCTL_JOCHONA_SET_RENDER_ADAPTER_LUID, &in, sizeof(in), nullptr, 0, bytes) != FALSE;
  }

  lease_handle_t::lease_handle_t(std::unique_ptr<impl_t> impl):
      impl_(std::move(impl)) {
  }

  lease_handle_t::lease_handle_t(lease_handle_t &&) noexcept = default;
  lease_handle_t &lease_handle_t::operator=(lease_handle_t &&) noexcept = default;

  lease_handle_t::~lease_handle_t() {
    release();
  }

  std::optional<error_e> lease_handle_t::configure(const slot_mode_t &mode) {
    if (!valid()) {
      return error_e::slot_not_leased;
    }

    JochonaConfigureSlotIn in {};
    in.RequestedVersion = current_protocol_version();
    in.SlotId = impl_->slot_id;
    in.LeaseToken = impl_->lease_token;
    in.Mode.Width = mode.width;
    in.Mode.Height = mode.height;
    in.Mode.RefreshNumerator = mode.refresh_numerator;
    in.Mode.RefreshDenominator = mode.refresh_denominator;
    in.Mode.BitsPerChannel = mode.bits_per_channel;
    in.Mode.HdrEnabled = mode.hdr_enabled ? 1 : 0;

    DWORD bytes = 0;
    std::lock_guard lock {impl_->client_impl->mutex};
    if (impl_->client_impl->device == INVALID_HANDLE_VALUE) {
      return error_e::driver_not_installed;
    }
    if (!impl_->client_impl->send(IOCTL_JOCHONA_CONFIGURE_SLOT, &in, sizeof(in), nullptr, 0, bytes)) {
      return map_last_error();
    }
    return std::nullopt;
  }

  void lease_handle_t::release() {
    if (!impl_ || impl_->released.exchange(true)) {
      return;
    }

    impl_->stop_flag.store(true, std::memory_order_release);
    if (impl_->watchdog_thread.joinable()) {
      impl_->watchdog_thread.join();
    }

    JochonaReleaseSlotIn in {};
    in.RequestedVersion = current_protocol_version();
    in.SlotId = impl_->slot_id;
    in.LeaseToken = impl_->lease_token;

    DWORD bytes = 0;
    std::lock_guard lock {impl_->client_impl->mutex};
    if (impl_->client_impl->device != INVALID_HANDLE_VALUE) {
      impl_->client_impl->send(IOCTL_JOCHONA_RELEASE_SLOT, &in, sizeof(in), nullptr, 0, bytes);
    }
  }

  bool lease_handle_t::valid() const {
    return impl_ && !impl_->released.load(std::memory_order_acquire);
  }

#else  // !_WIN32

  // The Jochona Display Adapter is a Windows 11 IddCx driver; every other
  // platform honestly reports it as unavailable rather than fabricating a
  // successful probe or lease.

  struct client_t::impl_t {};

  struct lease_handle_t::impl_t {};

  client_t::client_t():
      impl_(std::make_shared<impl_t>()) {
  }

  client_t &client_t::instance() {
    static client_t instance;
    return instance;
  }

  bool client_t::ensure_open() {
    return false;
  }

  manifest_status_t client_t::probe() {
    manifest_status_t status;
    status.installed = false;
    status.healthy = false;
    status.detail = "Jochona Display Adapter is a Windows 11 IddCx driver; not available on this platform.";
    return status;
  }

  std::variant<lease_handle_t, error_e> client_t::lease(const std::array<std::uint8_t, 16> &) {
    return error_e::driver_not_installed;
  }

  bool client_t::set_render_adapter_luid(std::uint32_t, std::int32_t) {
    return false;
  }

  lease_handle_t::lease_handle_t(std::unique_ptr<impl_t> impl):
      impl_(std::move(impl)) {
  }

  lease_handle_t::lease_handle_t(lease_handle_t &&) noexcept = default;
  lease_handle_t &lease_handle_t::operator=(lease_handle_t &&) noexcept = default;

  lease_handle_t::~lease_handle_t() = default;

  std::optional<error_e> lease_handle_t::configure(const slot_mode_t &) {
    return error_e::slot_not_leased;
  }

  void lease_handle_t::release() {
  }

  bool lease_handle_t::valid() const {
    return false;
  }

#endif  // _WIN32

}  // namespace jochona::display_adapter
