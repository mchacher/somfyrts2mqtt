/**
 * @file nvs_store.h
 * @brief Persistent store for MQTT config and Somfy virtual remotes (NVS-backed).
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>

/**
 * @namespace nvs_store
 * @brief Persistent store for MQTT broker config and Somfy virtual remotes.
 *
 * Backed by a single Arduino `Preferences` namespace named "somfy".
 * The pure helpers (validation, hex format/parse, CSV index manipulation)
 * are inline so native unit tests can exercise them without linking
 * against `Preferences`.
 */
namespace nvs_store {

  /// MQTT broker connection parameters.
  struct MqttConfig {
    std::string host;
    uint16_t    port = 1883;
    std::string user;
    std::string pass;
    /// Tasmota-style root topic (e.g. `somfyrts2mqtt-XXXXXX`). Empty string
    /// means "use the runtime default", which `mqtt::init()` derives from
    /// the hostname. Validated via `is_valid_topic()` on set.
    std::string topic;
  };

  /// A Somfy virtual remote stored in NVS.
  ///
  /// Time-based position estimation (iter 014) requires `open_time_ms` to be
  /// non-zero. `close_time_ms == 0` falls back to `open_time_ms`. `position`
  /// is the last persisted snapshot (we never write during motion to spare
  /// flash wear).
  struct Remote {
    uint32_t    id;            ///< 24-bit id (validated).
    uint16_t    rolling_code;
    std::string name;          ///< MQTT-safe; validated by is_valid_name().
    uint32_t    open_time_ms;  ///< 0 = uncalibrated.
    uint32_t    close_time_ms; ///< 0 = falls back to open_time_ms.
    uint8_t     position;      ///< 0 (closed) .. 100 (open).
    bool        invert;        ///< Swap Up <-> Down RF buttons (awnings).
    uint8_t     device_type;   ///< iter 022: 0 = Shutter (default). See device_profile::DeviceType.
    uint8_t     toggle_button;  ///< iter 022: gate toggle RTS button code (default 0x02 Up).
  };

  /// Hint to skip the WiFi scan on subsequent boots (Tasmota-style).
  struct WifiHint {
    std::string ssid;          ///< SSID the hint applies to.
    uint8_t     bssid[6] = {0};///< Last successful AP MAC.
    uint8_t     channel  = 0;  ///< Channel of the last successful AP.
  };

  /// Maximum number of simultaneous remotes the store supports.
  static constexpr size_t MAX_REMOTES = 16;

  /// Maximum allowed length of a remote name (in bytes).
  static constexpr size_t MAX_NAME_LEN = 32;

  /// Maximum allowed length of the MQTT root topic (in bytes).
  static constexpr size_t MAX_TOPIC_LEN = 64;

  /// Hex id width (uppercase chars, no NUL).
  static constexpr size_t ID_HEX_LEN = 6;

  /// Current schema version. iter 014 and iter 022 both kept this at 1 even
  /// though they added new Remote fields : reads on absent keys return the
  /// type-default (0 for uint, "" for string), which is the same value
  /// `add_remote()` would have written for a fresh entry. So old NVS layouts
  /// remain readable -- no migration code needed, and critically `init()` does
  /// NOT hit its "refusing to operate" branch on a box upgraded from an earlier
  /// firmware. A bump here is reserved for a genuinely breaking layout change.
  static constexpr uint8_t SCHEMA_VERSION = 1;

  // === Preferences-backed API (implemented in src/nvs_store.cpp) ===

  /**
   * @brief Open the Preferences namespace and verify the schema sentinel.
   *
   * Sets the internal ready flag on success. Logs an error and leaves
   * the store inactive on schema mismatch.
   */
  void init();

  /// @return whether init() succeeded and the store is operational.
  bool ready();

  /**
   * @brief Write MQTT broker config to NVS.
   * @return false if the store is not ready, host is empty, or port is zero.
   */
  bool set_mqtt(const MqttConfig& cfg);

  /// @return the stored MQTT config (defaults when fields are absent).
  MqttConfig get_mqtt();

  /**
   * @brief Add or update a virtual remote.
   *
   * If a remote with the same id already exists, its code and name are
   * updated. Returns false on validation failure or when capacity is
   * reached and the id is new.
   */
  bool add_remote(uint32_t id, uint16_t code, const std::string& name);

  /// @brief Update only the rolling code of an existing remote.
  bool update_rolling_code(uint32_t id, uint16_t new_code);

  /// @brief Persist the calibrated full-Open duration of a remote (ms).
  /// @return false if the remote is not registered.
  bool set_open_duration(uint32_t id, uint32_t open_time_ms);

  /// @brief Persist the calibrated full-Close duration of a remote (ms).
  /// @return false if the remote is not registered.
  bool set_close_duration(uint32_t id, uint32_t close_time_ms);

  /// @brief Persist the current position (0-100). Called by the orchestrator
  /// at motion stop or on `SetPosition` (manual calibration).
  /// @return false if the remote is not registered.
  bool update_position(uint32_t id, uint8_t position);

  /// @brief Persist the invert flag. When true, `Open`/`Close` commands swap
  /// the Up/Down RF buttons at emission. Useful for awnings ("store banne")
  /// whose physical Up button retracts (user-perceived "close").
  /// @return false if the remote is not registered.
  bool set_invert(uint32_t id, bool invert);

  /// @brief Persist the device type (iter 022). Raw byte; 0 = Shutter.
  /// Interpreted via `device_profile::DeviceType`. Kept as a plain uint8 so
  /// `nvs_store` carries no dependency on the profile enum.
  /// @return false if the remote is not registered.
  bool set_device_type(uint32_t id, uint8_t type);

  /// @brief Persist a gate's toggle RTS button (iter 022). Raw button code
  /// (0x01 My / 0x02 Up / 0x04 Down); validated by `device_profile::valid_toggle_button`
  /// at emission. Only meaningful for a Gate-type remote.
  /// @return false if the remote is not registered.
  bool set_toggle_button(uint32_t id, uint8_t button);

  /// @brief Remove a remote by id. Returns false if not present.
  bool delete_remote(uint32_t id);

  /**
   * @brief Look up a remote by id.
   * @param out  Filled on success.
   * @return false if not present.
   */
  bool get_remote(uint32_t id, Remote& out);

  /**
   * @brief Copy all stored remotes into @p out, up to @p max_count entries.
   * @return number of remotes written.
   */
  size_t list_remotes(Remote* out, size_t max_count);

  /// @return the number of remotes currently stored.
  size_t remotes_count();

  /**
   * @brief Read the persisted WiFi connection hint.
   * @param out  Filled on success.
   * @return false if the hint is absent, has an empty SSID, or has channel 0.
   */
  bool get_wifi_hint(WifiHint& out);

  /**
   * @brief Persist the WiFi connection hint (fast-path for subsequent boots).
   * @return false if the store is not ready or the hint is invalid.
   */
  bool set_wifi_hint(const WifiHint& hint);

  /// @brief Drop the persisted WiFi hint. Next boot will rescan.
  void clear_wifi_hint();

  // === WiFi credentials (iter 015) ===
  //
  // Separate from WifiHint: the hint is "which BSSID/channel did we last
  // associate with for the stored SSID?" while credentials are "what SSID
  // and password should we try at boot?". Splitting them lets the captive
  // portal write fresh creds without losing the BSSID pinning logic, and
  // lets the sticky-bad-BSSID recovery clear the hint without wiping the
  // creds.

  /// @brief Read the persisted WiFi SSID. Empty string when never set.
  std::string get_wifi_ssid();

  /// @brief Read the persisted WiFi password. Empty string when never set.
  std::string get_wifi_pass();

  /**
   * @brief Persist new WiFi credentials and invalidate the BSSID hint.
   *
   * Writes `wifi.ssid` + `wifi.pass`, then clears `wifi.bssid` /
   * `wifi.channel` so the next boot does a clean scan instead of trying
   * to associate with the previous router's BSSID.
   * @return false if the store is not ready or @p ssid is empty.
   */
  bool set_wifi_creds(const std::string& ssid, const std::string& pass);

  // === Boot counter (iter 015) ===
  //
  // 4-power-cycles AP-recovery counter. Incremented at every boot, reset
  // after a few seconds of stable uptime. See include/wifi_boot.h for the
  // pure-logic state machine.

  /// @brief Read the boot counter (0 if never set).
  uint8_t get_boot_count();

  /// @brief Persist the boot counter.
  /// @return false if the store is not ready.
  bool set_boot_count(uint8_t count);

  /**
   * @brief Wipe every key in the Preferences namespace and rewrite the
   *        schema sentinel.
   */
  void factory_reset();

  // === Pure helpers (inline; testable without Preferences) ===

  /// True when `0 < id <= 0xFFFFFF`.
  inline bool is_valid_id(uint32_t id) {
    return id > 0 && id <= 0xFFFFFFu;
  }

  /// True when @p name matches `[a-zA-Z0-9_-]{1, MAX_NAME_LEN}`. Tightened
  /// in iter 014 because the name is now embedded in MQTT topics ; spaces,
  /// slashes, dots, wildcards (`+`, `#`) etc. would break the routing.
  inline bool is_valid_name(const std::string& name) {
    if (name.empty() || name.size() > MAX_NAME_LEN) return false;
    for (char c : name) {
      const bool ok = (c >= 'a' && c <= 'z') ||
                      (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') ||
                       c == '_' || c == '-';
      if (!ok) return false;
    }
    return true;
  }

  /// True when @p topic is a valid MQTT root prefix : alnum, `_`, `-`, `/`
  /// only ; no leading or trailing slash ; no MQTT wildcards. Empty string
  /// is rejected (the runtime fallback is handled by `mqtt::init()`).
  inline bool is_valid_topic(const std::string& topic) {
    if (topic.empty() || topic.size() > MAX_TOPIC_LEN) return false;
    if (topic.front() == '/' || topic.back() == '/') return false;
    for (char c : topic) {
      const bool ok = (c >= 'a' && c <= 'z') ||
                      (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') ||
                       c == '_' || c == '-' || c == '/';
      if (!ok) return false;
    }
    return true;
  }

  /// Write the 6-char uppercase hex of @p id plus a trailing NUL into @p out.
  inline void format_id_hex(uint32_t id, char out[7]) {
    std::snprintf(out, 7, "%06X", static_cast<unsigned>(id & 0xFFFFFFu));
  }

  /**
   * @brief Parse a 6-char hex string into @p out.
   * @return false on null pointer, wrong length, or non-hex characters.
   */
  inline bool parse_id_hex(const char* in, uint32_t& out) {
    if (in == nullptr) return false;
    uint32_t v = 0;
    size_t n = 0;
    for (; n < 7 && in[n] != '\0'; ++n) {
      const char c = in[n];
      uint8_t nibble;
      if      (c >= '0' && c <= '9') nibble = static_cast<uint8_t>(c - '0');
      else if (c >= 'A' && c <= 'F') nibble = static_cast<uint8_t>(10 + c - 'A');
      else if (c >= 'a' && c <= 'f') nibble = static_cast<uint8_t>(10 + c - 'a');
      else return false;
      v = (v << 4) | nibble;
    }
    if (n != ID_HEX_LEN) return false;
    out = v;
    return true;
  }

  /// True when @p id_hex appears as a whole comma-bounded element of @p csv.
  inline bool index_contains(const std::string& csv, const char* id_hex) {
    if (id_hex == nullptr || csv.size() < ID_HEX_LEN) return false;
    size_t pos = 0;
    while (pos + ID_HEX_LEN <= csv.size()) {
      if (csv.compare(pos, ID_HEX_LEN, id_hex, ID_HEX_LEN) == 0) {
        const bool left_ok  = (pos == 0)                    || (csv[pos - 1] == ',');
        const bool right_ok = (pos + ID_HEX_LEN == csv.size()) || (csv[pos + ID_HEX_LEN] == ',');
        if (left_ok && right_ok) return true;
      }
      const size_t comma = csv.find(',', pos);
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
    return false;
  }

  /// Append @p id_hex to the CSV if not already present.
  inline void index_add(std::string& csv, const char* id_hex) {
    if (id_hex == nullptr || index_contains(csv, id_hex)) return;
    if (!csv.empty()) csv.push_back(',');
    csv.append(id_hex);
  }

  /// Remove @p id_hex from the CSV. @return true if it was present.
  inline bool index_remove(std::string& csv, const char* id_hex) {
    if (id_hex == nullptr || csv.size() < ID_HEX_LEN) return false;
    size_t pos = 0;
    while (pos + ID_HEX_LEN <= csv.size()) {
      if (csv.compare(pos, ID_HEX_LEN, id_hex, ID_HEX_LEN) == 0) {
        const bool left_ok  = (pos == 0)                    || (csv[pos - 1] == ',');
        const bool right_ok = (pos + ID_HEX_LEN == csv.size()) || (csv[pos + ID_HEX_LEN] == ',');
        if (left_ok && right_ok) {
          if (pos == 0 && pos + ID_HEX_LEN == csv.size()) {
            csv.clear();
          } else if (pos == 0) {
            csv.erase(0, ID_HEX_LEN + 1);              // include trailing comma
          } else {
            csv.erase(pos - 1, ID_HEX_LEN + 1);        // include leading comma
          }
          return true;
        }
      }
      const size_t comma = csv.find(',', pos);
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
    return false;
  }

}
