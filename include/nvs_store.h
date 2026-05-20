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
  };

  /// A Somfy virtual remote stored in NVS.
  struct Remote {
    uint32_t    id;            ///< 24-bit id (validated).
    uint16_t    rolling_code;
    std::string name;
  };

  /// Maximum number of simultaneous remotes the store supports.
  static constexpr size_t MAX_REMOTES = 16;

  /// Maximum allowed length of a remote name (in bytes).
  static constexpr size_t MAX_NAME_LEN = 32;

  /// Hex id width (uppercase chars, no NUL).
  static constexpr size_t ID_HEX_LEN = 6;

  /// Current schema version.
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
   * @brief Wipe every key in the Preferences namespace and rewrite the
   *        schema sentinel.
   */
  void factory_reset();

  // === Pure helpers (inline; testable without Preferences) ===

  /// True when `0 < id <= 0xFFFFFF`.
  inline bool is_valid_id(uint32_t id) {
    return id > 0 && id <= 0xFFFFFFu;
  }

  /// True when name length is in `[1, MAX_NAME_LEN]`.
  inline bool is_valid_name(const std::string& name) {
    return !name.empty() && name.size() <= MAX_NAME_LEN;
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
