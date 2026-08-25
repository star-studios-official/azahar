// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <functional>
#include "common/common_types.h"

namespace Core {
class System;
}

namespace TWL {

/// Framebuffer dimensions for the DS screens
constexpr u32 DS_SCREEN_WIDTH = 256;
constexpr u32 DS_SCREEN_HEIGHT = 192;
constexpr u32 DS_SCREEN_TOTAL_WIDTH = DS_SCREEN_WIDTH * 2; // Top + bottom side by side

/// Input mask for NDS buttons (matches NDS key bits)
enum Button : u16 {
    ButtonA = (1 << 0),
    ButtonB = (1 << 1),
    ButtonSelect = (1 << 2),
    ButtonStart = (1 << 3),
    ButtonRight = (1 << 4),
    ButtonLeft = (1 << 5),
    ButtonUp = (1 << 6),
    ButtonDown = (1 << 7),
    ButtonR = (1 << 8),
    ButtonL = (1 << 9),
    ButtonX = (1 << 10),
    ButtonY = (1 << 11),
};

/// Touch screen input state
struct TouchState {
    bool pressed = false;
    u16 x = 0;
    u16 y = 0;
};

/// Core TWL emulator wrapping melonDS
class Core {
public:
    explicit Core();
    ~Core();

    /// Initialize the emulator and load a ROM file
    bool Initialize(const std::string& nds_rom_path);

    /// Start the emulation loop in a background thread
    void Start();

    /// Stop the emulation loop
    void Stop();

    /// Check if the emulator is running
    bool IsRunning() const { return running.load(); }

    /// Set button state (OR of Button values)
    void SetButtonState(u16 buttons);

    /// Set touch screen state
    void SetTouchState(const TouchState& touch);

    /// Map 3DS button bitmask to NDS button bitmask
    /// 3DS: A=700,B=701,X=702,Y=703,Start=704,Select=705,ZL=707,ZR=708
    /// DPad=709-712, L=773, R=774
    static u16 Map3DSButtonsToNDS(u32 hbl_button_state, s16 circle_pad_x, s16 circle_pad_y);

    /// Get the top screen framebuffer (RGBA8888, 256x192)
    const u32* GetTopScreen() const { return top_screen.data(); }

    /// Get the bottom screen framebuffer (RGBA8888, 256x192)
    const u32* GetBottomScreen() const { return bottom_screen.data(); }

    /// Get screen width (pixels)
    u32 GetScreenWidth() const { return DS_SCREEN_WIDTH; }

    /// Get screen height (pixels)
    u32 GetScreenHeight() const { return DS_SCREEN_HEIGHT; }

    /// Set callback for when the NDS signals a stop
    void SetStopCallback(std::function<void()> callback) { stop_callback = std::move(callback); }

    /// Get the ROM title from the header
    std::string GetROMTitle() const;

    /// Get the ROM game code
    std::string GetGameCode() const;

    /// Check if the emulator was initialized successfully
    bool IsInitialized() const { return initialized; }

    /// Get error message if initialization failed
    const std::string& GetError() const { return error_message; }

    /// Set audio callback: called with interleaved stereo PCM16 samples.
    /// sample_rate is the rate melonDS is outputting at (32704 or 48000).
    void SetAudioCallback(std::function<void(const s16* samples, std::size_t num_samples, u32 sample_rate)> cb);

    /// Read pending audio samples from melonDS SPU (non-blocking).
    /// Returns the number of stereo samples written to out_buf.
    int ReadAudioOutput(s16* out_buf, int max_samples);

    /// Get the current output sample rate of the SPU
    u32 GetAudioSampleRate() const;

    /// Insert a GBA cart into the emulated GBA slot
    bool InsertGBACart(const std::string& gba_rom_path, const std::string& gba_save_path = "");

    /// Load a DS save file (.sav) into the inserted cartridge
    bool LoadDSSave(const std::string& save_path);

    /// Save the current DS save to a file
    bool SaveDSSave(const std::string& save_path);

private:
    /// Emulation loop (runs in background thread)
    void RunLoop();

    /// Copy screen data from melonDS output to our framebuffers
    void CopyScreens();

    /// Handle input state updates
    void UpdateInput();

    std::unique_ptr<std::thread> emu_thread;
    std::atomic<bool> running{false};
    std::atomic<bool> initialized{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<u16> button_state{0};
    TouchState touch_state;
    std::string error_message;

    // Screen buffers (RGBA8888)
    std::array<u32, DS_SCREEN_WIDTH * DS_SCREEN_HEIGHT> top_screen{};
    std::array<u32, DS_SCREEN_WIDTH * DS_SCREEN_HEIGHT> bottom_screen{};

    std::function<void()> stop_callback;
    std::function<void(const s16*, std::size_t, u32)> audio_callback;

    // melonDS state (opaque - forward declared in cpp)
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace TWL
