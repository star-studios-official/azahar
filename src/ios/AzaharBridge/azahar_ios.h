// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// C API bridging the Azahar C++ core to the iOS Swift frontend.
// This is the iOS equivalent of the Android JNI layer (src/android/app/src/main/jni).
// All functions are callable from Swift through the generated bridging header.

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Button codes (must match InputManager::ButtonType in the Android frontend)
// ---------------------------------------------------------------------------
enum {
    AZ_BUTTON_A = 700,
    AZ_BUTTON_B = 701,
    AZ_BUTTON_X = 702,
    AZ_BUTTON_Y = 703,
    AZ_BUTTON_START = 704,
    AZ_BUTTON_SELECT = 705,
    AZ_BUTTON_HOME = 706,
    AZ_BUTTON_ZL = 707,
    AZ_BUTTON_ZR = 708,
    AZ_DPAD_UP = 709,
    AZ_DPAD_DOWN = 710,
    AZ_DPAD_LEFT = 711,
    AZ_DPAD_RIGHT = 712,
    AZ_STICK_LEFT = 713,
    AZ_STICK_LEFT_UP = 714,
    AZ_STICK_LEFT_DOWN = 715,
    AZ_STICK_LEFT_LEFT = 716,
    AZ_STICK_LEFT_RIGHT = 717,
    AZ_STICK_C = 718,
    AZ_STICK_C_UP = 719,
    AZ_STICK_C_DOWN = 720,
    AZ_STICK_C_LEFT = 771,
    AZ_STICK_C_RIGHT = 772,
    AZ_TRIGGER_L = 773,
    AZ_TRIGGER_R = 774,
    AZ_BUTTON_DEBUG = 781,
    AZ_BUTTON_GPIO14 = 782,
};

// Core error codes (mirror NativeLibrary.CoreError)
enum {
    AZ_CORE_ERROR_SUCCESS = 0,
    AZ_CORE_ERROR_NOT_INITIALIZED = 1,
    AZ_CORE_ERROR_GET_LOADER = 2,
    AZ_CORE_ERROR_SYSTEM_MODE = 3,
    AZ_CORE_ERROR_LOADER = 4,
    AZ_CORE_ERROR_LOADER_ENCRYPTED = 5,
    AZ_CORE_ERROR_LOADER_INVALID_FORMAT = 6,
    AZ_CORE_ERROR_LOADER_GBA_TITLE = 7,
    AZ_CORE_ERROR_LOADER_PATCHES = 8,
    AZ_CORE_ERROR_LOADER_PATCHES_INVALID_TITLE = 9,
    AZ_CORE_ERROR_SYSTEM_FILES = 10,
    AZ_CORE_ERROR_SAVESTATE = 11,
    AZ_CORE_ERROR_ARTIC_DISCONNECTED = 12,
    AZ_CORE_ERROR_N3DS_APPLICATION = 13,
    AZ_CORE_ERROR_CORE_EXCEPTION = 14,
    AZ_CORE_ERROR_MEMORY_EXCEPTION = 15,
    AZ_CORE_ERROR_SHUTDOWN_REQUESTED = 16,
    AZ_CORE_ERROR_UNKNOWN = 17,
};

// Netplay status (mirror NetPlayStatus in the Android frontend)
enum {
    AZ_NETPLAY_SUCCESS = 0,
    AZ_NETPLAY_UNKNOWN_ERROR = 1,
    AZ_NETPLAY_UNABLE_TO_CONNECT = 2,
    AZ_NETPLAY_OUTDATED_CLIENT = 3,
    AZ_NETPLAY_OUTDATED_SERVICE = 4,
    AZ_NETPLAY_IP_ALREADY_CONNECTED = 5,
    AZ_NETPLAY_IP_IS_BANNED = 6,
    AZ_NETPLAY_MAC_IS_BANNED = 7,
    AZ_NETPLAY_CONSOLE_ID_IS_BANNED = 8,
    AZ_NETPLAY_ROOM_FULL = 9,
    AZ_NETPLAY_COULD_NOT_CREATE_ROOM = 10,
    AZ_NETPLAY_WRONG_VERSION = 11,
    AZ_NETPLAY_INVALID_PASSWORD = 12,
    AZ_NETPLAY_ROOM_NOT_FOUND = 13,
    AZ_NETPLAY_HOST_KICKED = 14,
    AZ_NETPLAY_PASSWORD_REQUIRED = 15,
    AZ_NETPLAY_NO_INTERNET = 16,
    AZ_NETPLAY_CONSOLE_ID_IN_USE = 17,
    AZ_NETPLAY_WRONG_ACCOUNT = 18,
    AZ_NETPLAY_CANNOT_JOIN_ONLY_ONE_NEW3DS = 19,
};

// Compression status (mirror NativeLibrary.CompressStatus)
enum {
    AZ_COMPRESS_SUCCESS = 0,
    AZ_COMPRESS_UNSUPPORTED = 1,
    AZ_COMPRESS_ALREADY_COMPRESSED = 2,
    AZ_COMPRESS_FAILED = 3,
    AZ_DECOMPRESS_UNSUPPORTED = 4,
    AZ_DECOMPRESS_NOT_COMPRESSED = 5,
    AZ_DECOMPRESS_FAILED = 6,
    AZ_COMPRESS_INSTALLED_APPLICATION = 7,
};

// Media type of an installed game (mirror Game.MediaType)
enum {
    AZ_MEDIA_TYPE_SDMC = 0,
    AZ_MEDIA_TYPE_NAND = 1,
};

#define AZ_SAVESTATE_SLOT_COUNT 11
#define AZ_QUICKSAVE_SLOT 0

typedef struct {
    int slot;
    int64_t timestamp_ms;
} az_savestate_info;

typedef struct {
    const char* path;
    int media_type;
} az_game_path;

typedef struct {
    const char* name;
    const char* title;
    const char* description;
    int max_players;
    int player_count;
    bool has_password;
    const char* game_name;
    int64_t game_id;
} az_room_entry;

typedef struct {
    const char* username;
    const char* nickname;
    int64_t console_id;
    int64_t ban_time_ms;
} az_player_entry;

// ---------------------------------------------------------------------------
// Lifecycle / user data
// ---------------------------------------------------------------------------

/// Sets the emulator user directory (equivalent to setUserDirectory). Should be
/// called once at startup, before config/log creation.
void az_set_user_directory(const char* directory);

/// Reads config.ini from the user directory and applies it to Settings. If the
/// file is missing it is created from the defaults.
void az_create_config_file(void);

/// Initializes and starts the log backend.
void az_create_log_file(void);

/// Initializes AES encryption keys needed for CIA file operations (NUS downloads, etc).
/// Should be called once on app startup after az_set_user_directory.
/// This allows NUS downloads to work without starting emulation.
void az_init_crypto(void);

/// Initializes network subsystem for local multiplayer (must be called before starting emulation).
/// Should be called once on app startup after az_set_user_directory.
void az_init_network(void);

/// Shuts down network subsystem (call on app termination).
void az_shutdown_network(void);

/// Re-reads config.ini and applies settings to the running core if powered on.
void az_reload_settings(void);

void az_log_device_info(void);
void az_set_portrait_mode(bool portrait);

// ---------------------------------------------------------------------------
// Callbacks the Swift side registers (replaces the JNI->Java callbacks)
// ---------------------------------------------------------------------------

/// Returned values are how the core should proceed.
typedef void (*az_on_alert_fn)(const char* title, const char* message, bool yes_no,
                               bool* result);
typedef void (*az_on_core_error_fn)(int error, const char* details, bool* can_continue);
typedef void (*az_on_exit_emulation_fn)(int result_code);
typedef void (*az_on_disk_cache_progress_fn)(int stage, int progress, int max);
typedef void (*az_on_netplay_message_fn)(int type, const char* message);
typedef void (*az_on_netplay_clear_chat_fn)(void);
typedef void (*az_on_compress_progress_fn)(int64_t total, int64_t current);

/// Keyboard / Mii selector results are delivered back through these functions
/// after the Swift UI has collected user input. See az_swkbd_* / az_mii_*.
typedef void (*az_on_swkbd_request_fn)(void);
typedef void (*az_on_mii_request_fn)(void);

void az_set_on_alert(az_on_alert_fn fn);
void az_set_on_core_error(az_on_core_error_fn fn);
void az_set_on_exit_emulation(az_on_exit_emulation_fn fn);
void az_set_on_disk_cache_progress(az_on_disk_cache_progress_fn fn);
void az_set_on_netplay_message(az_on_netplay_message_fn fn);
void az_set_on_netplay_clear_chat(az_on_netplay_clear_chat_fn fn);
void az_set_on_compress_progress(az_on_compress_progress_fn fn);
void az_set_on_swkbd_request(az_on_swkbd_request_fn fn);
void az_set_on_mii_request(az_on_mii_request_fn fn);

/// Global callback pointers shared across translation units (defined in
/// ios_bridge.mm). Internal use only.
extern az_on_alert_fn on_alert;
extern az_on_swkbd_request_fn on_swkbd_request;
extern az_on_mii_request_fn on_mii_request;

// ---------------------------------------------------------------------------
// Emulation control
// ---------------------------------------------------------------------------

/// Begins emulation of the given game path. This call blocks on the calling
/// thread until emulation ends, so call it on a dedicated background thread.
/// When it returns, inspect az_get_last_result() for how it ended.
void az_run(const char* path);

/// Returns the result code that ended the last az_run() session.
int az_get_last_result(void);

void az_pause_emulation(void);
void az_unpause_emulation(void);
void az_stop_emulation(void);
bool az_is_running(void);
bool az_is_paused(void);
int64_t az_get_running_title_id(void);

// ---------------------------------------------------------------------------
// Framebuffer / surface
// ---------------------------------------------------------------------------

/// Hands the bridge a CAMetalLayer for the primary display. `scale` is the
/// layer's contents scale (points-to-pixels). Call on the main thread.
void az_emu_surface_set(void* metal_layer, float scale);
void az_emu_surface_destroy(void);
bool az_is_surface_set(void);

/// Hands the bridge a CAMetalLayer for the secondary (external) display.
void az_emu_secondary_surface_set(void* metal_layer, float scale);
void az_emu_secondary_surface_destroy(void);

/// Presents the latest rendered frame. Call once per vsync from a CADisplayLink
/// (equivalent to the Android Choreographer doFrame()).
void az_present_frame(void);

/// Informs the renderer that the framebuffer layout should be recomputed.
void az_update_framebuffer(bool is_portrait);

void az_swap_screens(bool swap_screens, int rotation);

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool az_button_event(int button, bool pressed);
bool az_analog_event(int analog, float x, float y);
bool az_axis_event(int axis, float value);
bool az_touch_event(float x, float y, bool pressed);
void az_touch_moved(float x, float y);
bool az_secondary_touch_event(float x, float y, bool pressed);
void az_secondary_touch_moved(float x, float y);
void az_release_all_keys(void);

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

/// Reads a setting from config.ini (the group/section, key and default are
/// passed through). Returns a heap-allocated string the caller must free with
/// az_free_string().
char* az_setting_get_string(const char* group, const char* key, const char* default_value);
bool az_setting_get_bool(const char* group, const char* key, bool default_value);
long az_setting_get_int(const char* group, const char* key, long default_value);
double az_setting_get_float(const char* group, const char* key, double default_value);

/// Persists a setting to config.ini and immediately applies it to the running
/// core when relevant.
void az_setting_set_string(const char* group, const char* key, const char* value);
void az_setting_set_bool(const char* group, const char* key, bool value);
void az_setting_set_int(const char* group, const char* key, long value);
void az_setting_set_float(const char* group, const char* key, double value);

void az_set_temporary_frame_limit(double speed);
void az_disable_temporary_frame_limit(void);

// ---------------------------------------------------------------------------
// Games / titles
// ---------------------------------------------------------------------------

int64_t az_get_title_id(const char* path);

/// Extracts the game icon from a ROM file.
/// Returns the number of pixels (48*48=2304) or 0 if extraction fails.
/// The icon data is RGB565 format (16-bit per pixel).
/// The caller must provide a buffer of at least 2304 uint16_t elements.
int az_get_game_icon(const char* path, uint16_t* out_icon_data, int buffer_size);

/// Extended game metadata structure
typedef struct {
    char title[256];
    char publisher[256];
    int64_t play_time_seconds;
    uint64_t title_id;
} az_game_metadata;

/// Extracts extended game metadata (title, publisher, playtime)
/// Returns true if successful, false otherwise
bool az_get_game_metadata(const char* path, az_game_metadata* out_metadata);
bool az_get_is_system_title(const char* path);
bool az_are_keys_available(void);

/// Scans the user directory for installed games. Returns the number of entries
/// written (<= max_count).
int az_get_installed_game_paths(az_game_path* out, int max_count);

bool az_uninstall_title(int64_t title_id, int media_type);
bool az_native_file_exists(const char* path);

// System titles
const char* az_get_home_menu_path(int region);
int az_get_system_title_ids(int system_type, int region, int64_t* out, int max_count);
void az_get_are_system_titles_installed(bool* out); // out[0] = old3ds, out[1] = new3ds
void az_uninstall_system_files(bool old3ds);
bool az_is_full_console_linked(void);
void az_unlink_console(void);
/// Generate synthetic console-unique files (SecureInfo, LFCS, OTP, movable.sed)
/// for Nimbus/Pretendo without needing a real 3DS.
/// region: 0=JPN, 1=USA, 2=EUR, etc.
void az_generate_console_files(int region);
bool az_is_synthetic_console_data(void);

/// Downloads and installs a title from Nintendo Update Service (NUS).
/// Returns InstallStatus: 0=Success, 1=ErrorFailedToOpenFile, 2=ErrorFileNotFound, 
/// 3=ErrorAborted, 4=ErrorInvalid, 5=ErrorEncrypted
int az_download_title_from_nus(uint64_t title_id);

/// Ensures system save data (CFG archive, config file) exists on disk.
/// Must be called before booting the Home Menu so NAND system data is present.
void az_init_system_save_data(void);

// ---------------------------------------------------------------------------
// Save states / performance / play time
// ---------------------------------------------------------------------------

/// Returns the number of save states found, writing at most max_count entries.
int az_get_savestate_info(az_savestate_info* out, int max_count);
void az_save_state(int slot);
void az_load_state(int slot);
bool az_save_state_exists(int slot);

/// Takes a screenshot and saves it to the user's photo library
void az_take_screenshot(void);

/// Resets the emulated system
void az_reset(void);

/// Fills out[0..8] with {system_fps, game_fps, emulation_speed, time_vblank_interval,
/// time_hle_svc, time_hle_ipc, time_gpu, time_swap, time_remaining}.
void az_get_perf_stats(double* out);

void az_play_time_init(void);
void az_play_time_start(int64_t title_id);
void az_play_time_stop(void);
int64_t az_play_time_get(int64_t title_id);
int64_t az_play_time_get_current_title(void);

// ---------------------------------------------------------------------------
// Amiibo
// ---------------------------------------------------------------------------

bool az_load_amiibo(const char* path);
void az_remove_amiibo(void);

// ---------------------------------------------------------------------------
// ROM/CIA compression
// ---------------------------------------------------------------------------

int az_compress_file(const char* input_path, const char* output_path);
int az_decompress_file(const char* input_path, const char* output_path);
char* az_get_recommended_extension(const char* input_path, bool compress);

// ---------------------------------------------------------------------------
// Software keyboard applet
// ---------------------------------------------------------------------------

/// Returns a heap-allocated string with the current swkbd config (see
/// az_swkbd_config struct in azahar_ios_internal.h). The Swift side consumes it
/// when az_on_swkbd_request is invoked.
char* az_swkbd_get_config(void);

/// Submits the software keyboard result. `button` is the index of the selected
/// button (0-based). Returns true if the input passed validation.
bool az_swkbd_submit(const char* text, int button);

/// Cancels the currently shown software keyboard.
void az_swkbd_cancel(void);

// ---------------------------------------------------------------------------
// Mii selector applet
// ---------------------------------------------------------------------------

/// Selects the Mii at the given index (0 = Standard Mii, 1..N = user Miis).
/// Returns true if the selection was accepted.
bool az_mii_select(int index);
void az_mii_cancel(void);

// ---------------------------------------------------------------------------
// Netplay / multiplayer
// ---------------------------------------------------------------------------

void az_netplay_init(void);
void az_netplay_shutdown(void);

int az_netplay_get_public_rooms(az_room_entry* out, int max_count);
int az_netplay_create_room(const char* ip, int port, const char* username,
                           const char* preferred_game_name, int64_t preferred_game_id,
                           const char* password, const char* room_name, int max_players);
int az_netplay_join_room(const char* ip, int port, const char* username, const char* password);
int az_netplay_get_room_info(az_room_entry* out);
bool az_netplay_is_joined(void);
bool az_netplay_is_hosted_room(void);
void az_netplay_send_message(const char* message);
void az_netplay_kick_user(const char* username);
void az_netplay_leave_room(void);
bool az_netplay_is_moderator(void);
int az_netplay_get_ban_list(char** out_usernames, int max_count);
void az_netplay_ban_user(const char* username);
void az_netplay_unban_user(const char* username);

// ---------------------------------------------------------------------------
// Cheats
// ---------------------------------------------------------------------------

typedef struct {
    int64_t cheat_id;
    const char* name;
    const char* notes;
    bool enabled;
} az_cheat_entry;

typedef struct {
    int64_t gate_id;
    const char* gate_name;
    int cheat_count;
} az_cheat_gate_entry;

/// Returns the number of cheat entries, writing at most max_count. The path
/// points to a .txt cheat file (or NULL to load the built-in cheat database).
int az_cheats_load(const char* path, az_cheat_entry* out, int max_count);
bool az_cheats_set_enabled(int64_t cheat_id, bool enabled);
bool az_cheats_apply(void);

// ---------------------------------------------------------------------------
// ZipPass (StreetPass export/import)
// ---------------------------------------------------------------------------

/// Export StreetPass data to a zip file
int az_zippass_export(const char* path);

/// Import StreetPass data from a zip file
int az_zippass_import(const char* path);

/// Import queued ZipPass data
int az_zippass_import_queued(void);

/// Clear StreetPass configuration
int az_zippass_clear_config(void);

// ---------------------------------------------------------------------------
// System Files
// ---------------------------------------------------------------------------

/// Install a system title from CIA
int az_install_cia(const char* path);

/// Check if system files are available
bool az_system_files_available(void);

/// Get system file status for a region (0=JPN, 1=USA, 2=EUR, etc)
bool az_system_files_region_available(int region);

/// Check if specific system archives exist
bool az_shared_font_available(void);
bool az_bad_word_list_available(void);
bool az_region_manifest_available(void);
bool az_home_menu_available(void);
bool az_mii_maker_available(void);

/// Check if seeddb.bin exists
bool az_seeddb_available(void);

/// Check if bootrom files exist (optional, not required for most games)
bool az_bootrom9_available(void);
bool az_bootrom11_available(void);
bool az_secret_sector_available(void);

/// Check if DSP firmware exists (optional, for audio processing)
bool az_dsp_firmware_available(void);

// ---------------------------------------------------------------------------
// RetroAchievements
// ---------------------------------------------------------------------------

/// User info structure
typedef struct {
    const char* username;
    const char* display_name;
    unsigned int score;
    unsigned int score_softcore;
    const char* token;
    const char* avatar_url;
} az_ra_user_t;

/// Achievement info structure
typedef struct {
    unsigned int id;
    const char* title;
    const char* description;
    const char* badge_url;
    unsigned int points;
    bool unlocked;
    bool hardcore;
    const char* progress_indicator;
    float progress_percent;
} az_ra_achievement_t;

/// Leaderboard info structure
typedef struct {
    unsigned int id;
    const char* title;
    const char* description;
    unsigned int num_entries;
} az_ra_leaderboard_t;

/// Leaderboard entry structure
typedef struct {
    unsigned int rank;
    const char* username;
    const char* score;
    int64_t timestamp;
} az_ra_leaderboard_entry_t;

/// Game info structure
typedef struct {
    unsigned int id;
    const char* title;
    const char* badge_url;
    unsigned int num_achievements;
    unsigned int num_unlocked;
    unsigned int num_leaderboards;
} az_ra_game_t;

/// Event types for callbacks
enum {
    AZ_RA_EVENT_ACHIEVEMENT_TRIGGERED = 0,
    AZ_RA_EVENT_LEADERBOARD_STARTED = 1,
    AZ_RA_EVENT_LEADERBOARD_SUBMITTED = 2,
    AZ_RA_EVENT_CHALLENGE_INDICATOR_SHOW = 3,
    AZ_RA_EVENT_CHALLENGE_INDICATOR_HIDE = 4,
    AZ_RA_EVENT_PROGRESS_INDICATOR_SHOW = 5,
    AZ_RA_EVENT_PROGRESS_INDICATOR_HIDE = 6,
    AZ_RA_EVENT_PROGRESS_INDICATOR_UPDATE = 7,
    AZ_RA_EVENT_LEADERBOARD_TRACKER_SHOW = 8,
    AZ_RA_EVENT_LEADERBOARD_TRACKER_HIDE = 9,
    AZ_RA_EVENT_LEADERBOARD_TRACKER_UPDATE = 10,
};

/// Event callback for achievements, leaderboards, challenges, and progress
typedef void (*az_ra_event_callback)(int event_type, const char* title, const char* description, const char* badge_url, const char* value);

/// Set event callback
void az_ra_set_event_callback(az_ra_event_callback callback);

/// Login with username and password
void az_ra_login(const char* username, const char* password);

/// Login with username and token
void az_ra_login_with_token(const char* username, const char* token);

/// Logout
void az_ra_logout(void);

/// Check if logged in
bool az_ra_is_logged_in(void);

/// Get current user info (returns NULL if not logged in)
const az_ra_user_t* az_ra_get_user(void);

/// Get current game info (returns NULL if no game loaded)
const az_ra_game_t* az_ra_get_game(void);

/// Get achievement list for current game
/// Returns number of achievements written to out buffer
/// Pass NULL to get count without writing
int az_ra_get_achievements(az_ra_achievement_t* out, int max_count);

/// Get leaderboard list for current game
/// Returns number of leaderboards written to out buffer
/// Pass NULL to get count without writing
int az_ra_get_leaderboards(az_ra_leaderboard_t* out, int max_count);

/// Fetch image data from URL (async)
typedef void (*az_ra_image_callback)(const unsigned char* data, int size);
void az_ra_fetch_image(const char* url, az_ra_image_callback callback);

/// Enable/disable RetroAchievements
void az_ra_set_enabled(bool enabled);

/// Check if RetroAchievements is enabled
bool az_ra_is_enabled(void);

/// Enable/disable hardcore mode
void az_ra_set_hardcore_enabled(bool enabled);

/// Check if hardcore mode is enabled
bool az_ra_is_hardcore_enabled(void);

/// Check if hardcore mode can be paused (for showing menus)
bool az_ra_can_pause_hardcore(void);

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

void az_free_string(char* str);

/// Current git/build version strings.
const char* az_get_version_string(void);

// ---------------------------------------------------------------------------
// JIT / System Info
// ---------------------------------------------------------------------------

/// Get current process ID
int32_t get_current_pid(void);

/// Get current bundle identifier (returns static string, no need to free)
const char* get_current_bundle_id(void);

/// Log message from Swift (for comprehensive "All" logging)
/// level: 0=Info, 1=Debug, 2=Warning, 3=Error, 4=Critical
void az_log_message(int level, const char* message);

/// Apply log filter level (applies immediately to running system)
/// level: -1=All(Trace), 0=Trace, 1=Debug, 2=Info, 3=Warning, 4=Error, 5=Critical
void az_apply_log_filter_level(int level);

// ---------------------------------------------------------------------------
// NWM Local Wireless (MultipeerConnectivity) Bridge
// ---------------------------------------------------------------------------

/// Initialize MultipeerConnectivity manager for local wireless
void az_nwm_init_multipeer(void);

/// Start hosting a local wireless network
/// @param room_name Display name for the room
/// @param title_id Game title ID (hex string)
/// @param game_title Human-readable game title
void az_nwm_start_hosting(const char* room_name, const char* title_id, const char* game_title);

/// Start browsing for available local wireless networks
void az_nwm_start_browsing(void);

/// Connect to a specific peer by name
void az_nwm_connect_to_peer(const char* peer_name);

/// Send packet to all connected peers
/// @param data Packet data
/// @param length Packet length in bytes
void az_nwm_send_packet(const uint8_t* data, size_t length);

/// Stop all local wireless sessions
void az_nwm_stop_all(void);

/// Check if there are received packets available
bool az_nwm_has_received_packets(void);

/// Pull a received packet (returns nullptr if no packets available)
/// @return Pointer to packet data (valid until next call to az_nwm_pull_packet)
void* az_nwm_pull_packet(void);

// Callbacks from Swift to C++ (implemented in ios_bridge.mm)
void az_nwm_hosting_started(void);
void az_nwm_peer_connected(const char* peer_name);
void az_nwm_peer_disconnected(const char* peer_name);
void az_nwm_receive_packet(const void* data, size_t length);

// ---------------------------------------------------------------------------
// Game card emulation
// ---------------------------------------------------------------------------

/// Inserts a .3ds/.cci file as a virtual game card. The file must be NCSD format.
/// Returns true on success, false if the file is not a valid game card image.
bool az_insert_cartridge(const char* path);

/// Ejects the virtual game card.
void az_eject_cartridge(void);

/// Returns true if a virtual game card is currently inserted.
bool az_is_cartridge_inserted(void);

#ifdef __cplusplus
}
#endif
