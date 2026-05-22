/**
 * @file nvs_store.cpp
 * @brief Preferences-backed implementation of nvs_store. See nvs_store.h.
 */
#include "nvs_store.h"
#include <Preferences.h>
#include <cstring>
#include "logger.h"

namespace nvs_store {

  static Preferences s_prefs;
  static bool        s_ready = false;
  static constexpr const char* NS = "somfy";

  // --- helpers (file-local) ---

  static std::string remote_code_key(const char* hex) {
    std::string s;
    s.reserve(13);
    s.append("r.").append(hex).append(".code");
    return s;
  }

  static std::string remote_name_key(const char* hex) {
    std::string s;
    s.reserve(13);
    s.append("r.").append(hex).append(".name");
    return s;
  }

  // iter 014: short suffixes ("opt", "clt", "pos") keep the full key
  // under the ESP32 NVS 15-char limit ("r.A1B2C3.opt" = 12 chars).
  static std::string remote_open_key(const char* hex) {
    std::string s;
    s.reserve(13);
    s.append("r.").append(hex).append(".opt");
    return s;
  }

  static std::string remote_close_key(const char* hex) {
    std::string s;
    s.reserve(13);
    s.append("r.").append(hex).append(".clt");
    return s;
  }

  static std::string remote_position_key(const char* hex) {
    std::string s;
    s.reserve(13);
    s.append("r.").append(hex).append(".pos");
    return s;
  }

  static std::string remote_invert_key(const char* hex) {
    std::string s;
    s.reserve(13);
    s.append("r.").append(hex).append(".inv");
    return s;
  }

  static std::string read_index() {
    return std::string(s_prefs.getString("r.index", "").c_str());
  }

  static void write_index(const std::string& idx) {
    s_prefs.putString("r.index", idx.c_str());
  }

  // --- lifecycle ---

  void init() {
    if (!s_prefs.begin(NS, false)) {
      logger::err("nvs", "Preferences.begin failed");
      return;
    }
    uint8_t schema = s_prefs.getUChar("schema", 0);
    if (schema == 0) {
      s_prefs.putUChar("schema", SCHEMA_VERSION);
      schema = SCHEMA_VERSION;
      logger::info("nvs", "fresh init, schema=%u", schema);
    } else if (schema != SCHEMA_VERSION) {
      logger::err("nvs", "unknown schema=%u, refusing to operate", schema);
      return;
    }
    // Idempotently pre-create the expected keys so subsequent reads
    // don't trigger spurious NOT_FOUND error logs from the NVS layer.
    if (!s_prefs.isKey("r.index"))     s_prefs.putString("r.index", "");
    if (!s_prefs.isKey("mqtt.host"))   s_prefs.putString("mqtt.host", "");
    if (!s_prefs.isKey("mqtt.port"))   s_prefs.putUShort("mqtt.port", 1883);
    if (!s_prefs.isKey("mqtt.user"))   s_prefs.putString("mqtt.user", "");
    if (!s_prefs.isKey("mqtt.pass"))   s_prefs.putString("mqtt.pass", "");
    if (!s_prefs.isKey("mqtt.topic"))  s_prefs.putString("mqtt.topic", "");
    if (!s_prefs.isKey("wifi.ssid"))   s_prefs.putString("wifi.ssid", "");
    if (!s_prefs.isKey("wifi.bssid"))  s_prefs.putString("wifi.bssid", "");
    if (!s_prefs.isKey("wifi.channel"))s_prefs.putUChar("wifi.channel", 0);
    s_ready = true;
    logger::info("nvs", "ready schema=%u remotes=%u",
                 schema, static_cast<unsigned>(remotes_count()));
  }

  bool ready() {
    return s_ready;
  }

  // --- MQTT ---

  bool set_mqtt(const MqttConfig& cfg) {
    if (!s_ready || cfg.host.empty() || cfg.port == 0) return false;
    // Empty topic is allowed (means "use runtime default") ; a non-empty
    // topic must satisfy is_valid_topic.
    if (!cfg.topic.empty() && !is_valid_topic(cfg.topic)) return false;
    s_prefs.putString("mqtt.host",  cfg.host.c_str());
    s_prefs.putUShort("mqtt.port",  cfg.port);
    s_prefs.putString("mqtt.user",  cfg.user.c_str());
    s_prefs.putString("mqtt.pass",  cfg.pass.c_str());
    s_prefs.putString("mqtt.topic", cfg.topic.c_str());
    return true;
  }

  MqttConfig get_mqtt() {
    MqttConfig cfg;
    if (!s_ready) return cfg;
    cfg.host  = s_prefs.getString("mqtt.host",  "").c_str();
    cfg.port  = s_prefs.getUShort("mqtt.port",  1883);
    cfg.user  = s_prefs.getString("mqtt.user",  "").c_str();
    cfg.pass  = s_prefs.getString("mqtt.pass",  "").c_str();
    cfg.topic = s_prefs.getString("mqtt.topic", "").c_str();
    return cfg;
  }

  // --- Remotes ---

  size_t remotes_count() {
    if (!s_ready) return 0;
    const std::string idx = read_index();
    if (idx.empty()) return 0;
    size_t count = 1;
    for (char c : idx) {
      if (c == ',') ++count;
    }
    return count;
  }

  bool add_remote(uint32_t id, uint16_t code, const std::string& name) {
    if (!s_ready) return false;
    if (!is_valid_id(id) || !is_valid_name(name)) return false;

    char hex[7];
    format_id_hex(id, hex);

    std::string idx = read_index();
    const bool is_new = !index_contains(idx, hex);
    if (is_new && remotes_count() >= MAX_REMOTES) return false;

    s_prefs.putUShort(remote_code_key(hex).c_str(), code);
    s_prefs.putString(remote_name_key(hex).c_str(), name.c_str());
    if (is_new) {
      // Initialise iter 014 fields to "uncalibrated / closed". Existing
      // remotes keep whatever duration / position they had.
      s_prefs.putULong(remote_open_key(hex).c_str(),     0u);
      s_prefs.putULong(remote_close_key(hex).c_str(),    0u);
      s_prefs.putUChar(remote_position_key(hex).c_str(), 0u);
      s_prefs.putUChar(remote_invert_key(hex).c_str(),   0u);
      index_add(idx, hex);
      write_index(idx);
    }
    logger::info("nvs", "remote +%s code=%u name=%s",
                 hex, code, name.c_str());
    return true;
  }

  bool set_open_duration(uint32_t id, uint32_t open_time_ms) {
    if (!s_ready || !is_valid_id(id)) return false;
    char hex[7];
    format_id_hex(id, hex);
    const std::string idx = read_index();
    if (!index_contains(idx, hex)) return false;
    s_prefs.putULong(remote_open_key(hex).c_str(), open_time_ms);
    return true;
  }

  bool set_close_duration(uint32_t id, uint32_t close_time_ms) {
    if (!s_ready || !is_valid_id(id)) return false;
    char hex[7];
    format_id_hex(id, hex);
    const std::string idx = read_index();
    if (!index_contains(idx, hex)) return false;
    s_prefs.putULong(remote_close_key(hex).c_str(), close_time_ms);
    return true;
  }

  bool update_position(uint32_t id, uint8_t position) {
    if (!s_ready || !is_valid_id(id)) return false;
    if (position > 100) position = 100;
    char hex[7];
    format_id_hex(id, hex);
    const std::string idx = read_index();
    if (!index_contains(idx, hex)) return false;
    s_prefs.putUChar(remote_position_key(hex).c_str(), position);
    return true;
  }

  bool set_invert(uint32_t id, bool invert) {
    if (!s_ready || !is_valid_id(id)) return false;
    char hex[7];
    format_id_hex(id, hex);
    const std::string idx = read_index();
    if (!index_contains(idx, hex)) return false;
    s_prefs.putUChar(remote_invert_key(hex).c_str(), invert ? 1u : 0u);
    return true;
  }

  bool update_rolling_code(uint32_t id, uint16_t new_code) {
    if (!s_ready || !is_valid_id(id)) return false;
    char hex[7];
    format_id_hex(id, hex);
    const std::string idx = read_index();
    if (!index_contains(idx, hex)) return false;
    s_prefs.putUShort(remote_code_key(hex).c_str(), new_code);
    return true;
  }

  bool delete_remote(uint32_t id) {
    if (!s_ready || !is_valid_id(id)) return false;
    char hex[7];
    format_id_hex(id, hex);
    std::string idx = read_index();
    if (!index_remove(idx, hex)) return false;
    s_prefs.remove(remote_code_key(hex).c_str());
    s_prefs.remove(remote_name_key(hex).c_str());
    s_prefs.remove(remote_open_key(hex).c_str());
    s_prefs.remove(remote_close_key(hex).c_str());
    s_prefs.remove(remote_position_key(hex).c_str());
    s_prefs.remove(remote_invert_key(hex).c_str());
    write_index(idx);
    logger::info("nvs", "remote -%s", hex);
    return true;
  }

  bool get_remote(uint32_t id, Remote& out) {
    if (!s_ready || !is_valid_id(id)) return false;
    char hex[7];
    format_id_hex(id, hex);
    const std::string idx = read_index();
    if (!index_contains(idx, hex)) return false;
    out.id            = id;
    out.rolling_code  = s_prefs.getUShort(remote_code_key(hex).c_str(),     0);
    out.name          = s_prefs.getString(remote_name_key(hex).c_str(),     "").c_str();
    out.open_time_ms  = s_prefs.getULong (remote_open_key(hex).c_str(),     0u);
    out.close_time_ms = s_prefs.getULong (remote_close_key(hex).c_str(),    0u);
    out.position      = s_prefs.getUChar (remote_position_key(hex).c_str(), 0u);
    out.invert        = s_prefs.getUChar (remote_invert_key(hex).c_str(),   0u) != 0u;
    return true;
  }

  size_t list_remotes(Remote* out, size_t max_count) {
    if (!s_ready || out == nullptr || max_count == 0) return 0;
    const std::string idx = read_index();
    if (idx.empty()) return 0;

    size_t written = 0;
    size_t pos = 0;
    while (pos < idx.size() && written < max_count) {
      const size_t comma = idx.find(',', pos);
      const size_t end   = (comma == std::string::npos) ? idx.size() : comma;
      if (end - pos == ID_HEX_LEN) {
        char hex[7];
        std::memcpy(hex, idx.data() + pos, ID_HEX_LEN);
        hex[ID_HEX_LEN] = '\0';
        uint32_t parsed_id = 0;
        if (parse_id_hex(hex, parsed_id)) {
          Remote& r = out[written];
          r.id            = parsed_id;
          r.rolling_code  = s_prefs.getUShort(remote_code_key(hex).c_str(),     0);
          r.name          = s_prefs.getString(remote_name_key(hex).c_str(),     "").c_str();
          r.open_time_ms  = s_prefs.getULong (remote_open_key(hex).c_str(),     0u);
          r.close_time_ms = s_prefs.getULong (remote_close_key(hex).c_str(),    0u);
          r.position      = s_prefs.getUChar (remote_position_key(hex).c_str(), 0u);
          r.invert        = s_prefs.getUChar (remote_invert_key(hex).c_str(),   0u) != 0u;
          ++written;
        }
      }
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
    return written;
  }

  // --- WiFi hint ---

  static void bssid_to_hex(const uint8_t bssid[6], char out[13]) {
    std::snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
                  bssid[0], bssid[1], bssid[2],
                  bssid[3], bssid[4], bssid[5]);
  }

  static bool bssid_from_hex(const char* in, uint8_t out[6]) {
    if (in == nullptr || std::strlen(in) != 12) return false;
    for (size_t i = 0; i < 6; ++i) {
      unsigned byte = 0;
      if (std::sscanf(in + i * 2, "%2x", &byte) != 1) return false;
      out[i] = static_cast<uint8_t>(byte);
    }
    return true;
  }

  bool get_wifi_hint(WifiHint& out) {
    if (!s_ready) return false;
    const std::string ssid = s_prefs.getString("wifi.ssid", "").c_str();
    if (ssid.empty()) return false;
    const std::string bssid_hex = s_prefs.getString("wifi.bssid", "").c_str();
    if (bssid_hex.empty()) return false;
    if (!bssid_from_hex(bssid_hex.c_str(), out.bssid)) return false;
    const uint8_t channel = s_prefs.getUChar("wifi.channel", 0);
    if (channel == 0 || channel > 14) return false;
    out.ssid    = ssid;
    out.channel = channel;
    return true;
  }

  bool set_wifi_hint(const WifiHint& hint) {
    if (!s_ready) return false;
    if (hint.ssid.empty() || hint.channel == 0 || hint.channel > 14) return false;
    char hex[13];
    bssid_to_hex(hint.bssid, hex);
    s_prefs.putString("wifi.ssid",    hint.ssid.c_str());
    s_prefs.putString("wifi.bssid",   hex);
    s_prefs.putUChar ("wifi.channel", hint.channel);
    logger::info("nvs", "wifi hint saved ssid=%s bssid=%s ch=%u",
                 hint.ssid.c_str(), hex, hint.channel);
    return true;
  }

  void clear_wifi_hint() {
    if (!s_ready) return;
    s_prefs.remove("wifi.ssid");
    s_prefs.remove("wifi.bssid");
    s_prefs.remove("wifi.channel");
    logger::info("nvs", "wifi hint cleared");
  }

  void factory_reset() {
    // Begin the namespace if init() never ran (or failed).
    if (!s_ready) {
      if (!s_prefs.begin(NS, false)) {
        logger::err("nvs", "factory_reset: Preferences.begin failed");
        return;
      }
    }
    s_prefs.clear();
    s_prefs.putUChar("schema", SCHEMA_VERSION);
    s_ready = true;
    logger::info("nvs", "factory reset, schema=%u", SCHEMA_VERSION);
  }

}
