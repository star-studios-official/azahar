// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/twl/twl_core.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <shared_mutex>

#include "common/file_util.h"
#include "common/logging/log.h"

// melonDS includes
#include "NDS.h"
#include "NDSCart.h"
#include "Args.h"
#include "GPU.h"
#include "SPU.h"
#include "RTC.h"
#include "SPI.h"
#include "FreeBIOS.h"
#include "SPI_Firmware.h"
#include "Platform.h"
#include "GBACart.h"
#include "DSi_NAND.h"
#include "DSi.h"
#include "net/Net.h"
#include "net/Net_Slirp.h"

// ---------------------------------------------------------------------------
// Shared state between melonDS Platform callbacks and TWL::Core
// These are at file scope so both namespaces can access them.
// ---------------------------------------------------------------------------

namespace {

struct PlatformState {
    std::string base_path;
    std::string nds_save_path;
    std::string firmware_path;
    std::atomic<u16> buttons{0};
    TWL::TouchState touch;
    std::function<void()> stop_callback;

    // Save data
    std::vector<u8> save_data;
    bool save_dirty = false;
    std::shared_mutex save_mutex;

    // Networking (WFC / Wiimmfi)
    melonDS::Net net;
    bool net_initialized = false;
};

static PlatformState g_platform_state;

// File handle wrapper for melonDS
struct FileHandleImpl {
    FILE* fp = nullptr;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// melonDS Platform implementation (required by melonDS core)
// This MUST be in ::melonDS::Platform, NOT inside namespace TWL.
// ---------------------------------------------------------------------------

namespace melonDS::Platform {

std::string GetLocalFilePath(const std::string& filename) {
    return g_platform_state.base_path + "/" + filename;
}

FileHandle* OpenFile(const std::string& path, FileMode mode) {
    std::string fopen_mode;
    if ((mode & FileMode::ReadWrite) == FileMode::ReadWrite) {
        fopen_mode = "r+b";
        if (!(mode & FileMode::Preserve))
            fopen_mode = "w+b";
    } else if (mode & FileMode::Write) {
        fopen_mode = "w+b";
        if (mode & FileMode::Append)
            fopen_mode = "ab";
    } else {
        fopen_mode = "rb";
    }

    if (mode & FileMode::Text)
        fopen_mode += "t";
    else
        fopen_mode += "b";

    FILE* fp = fopen(path.c_str(), fopen_mode.c_str());
    if (!fp)
        return nullptr;

    auto* handle = new FileHandleImpl();
    handle->fp = fp;
    return reinterpret_cast<FileHandle*>(handle);
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode) {
    return OpenFile(GetLocalFilePath(path), mode);
}

bool FileExists(const std::string& name) {
    return FileUtil::Exists(name);
}

bool LocalFileExists(const std::string& name) {
    return FileExists(GetLocalFilePath(name));
}

bool CheckFileWritable(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "ab");
    if (fp) {
        fclose(fp);
        return true;
    }
    return false;
}

bool CheckLocalFileWritable(const std::string& filepath) {
    return CheckFileWritable(GetLocalFilePath(filepath));
}

bool CloseFile(FileHandle* file) {
    if (!file)
        return false;
    auto* impl = reinterpret_cast<FileHandleImpl*>(file);
    bool ok = fclose(impl->fp) == 0;
    delete impl;
    return ok;
}

bool IsEndOfFile(FileHandle* file) {
    if (!file)
        return true;
    auto* impl = reinterpret_cast<FileHandleImpl*>(file);
    return feof(impl->fp) != 0;
}

bool FileReadLine(char* str, int count, FileHandle* file) {
    if (!file)
        return false;
    auto* impl = reinterpret_cast<FileHandleImpl*>(file);
    return fgets(str, count, impl->fp) != nullptr;
}

u64 FilePosition(FileHandle* file) {
    if (!file)
        return 0;
    auto* impl = reinterpret_cast<FileHandleImpl*>(file);
    return static_cast<u64>(ftell(impl->fp));
}

bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin) {
    if (!file)
        return false;
    auto* impl = reinterpret_cast<FileHandleImpl*>(file);
    int whence;
    switch (origin) {
    case FileSeekOrigin::Start:
        whence = SEEK_SET;
        break;
    case FileSeekOrigin::Current:
        whence = SEEK_CUR;
        break;
    case FileSeekOrigin::End:
        whence = SEEK_END;
        break;
    default:
        return false;
    }
    return fseek(impl->fp, offset, whence) == 0;
}

void FileRewind(FileHandle* file) {
    if (!file)
        return;
    auto* impl = reinterpret_cast<FileHandleImpl*>(file);
    rewind(impl->fp);
}

u64 FileRead(void* data, u64 size, u64 count, FileHandle* file) {
    if (!file)
        return 0;
    auto* impl = reinterpret_cast<FileHandleImpl*>(file);
    return fread(data, size, count, impl->fp);
}

bool FileFlush(FileHandle* file) {
    if (!file)
        return false;
    auto* impl = reinterpret_cast<FileHandleImpl*>(file);
    return fflush(impl->fp) == 0;
}

u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file) {
    if (!file)
        return 0;
    auto* impl = reinterpret_cast<FileHandleImpl*>(file);
    return fwrite(data, size, count, impl->fp);
}

u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...) {
    if (!file)
        return 0;
    auto* impl = reinterpret_cast<FileHandleImpl*>(file);
    va_list args;
    va_start(args, fmt);
    u64 result = vfprintf(impl->fp, fmt, args);
    va_end(args);
    return result;
}

u64 FileLength(FileHandle* file) {
    if (!file)
        return 0;
    auto* impl = reinterpret_cast<FileHandleImpl*>(file);
    long pos = ftell(impl->fp);
    fseek(impl->fp, 0, SEEK_END);
    long len = ftell(impl->fp);
    fseek(impl->fp, pos, SEEK_SET);
    return static_cast<u64>(len);
}

void Log(LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    switch (level) {
    case LogLevel::Debug:
        LOG_DEBUG(TWL, "{}", buf);
        break;
    case LogLevel::Info:
        LOG_INFO(TWL, "{}", buf);
        break;
    case LogLevel::Warn:
        LOG_WARNING(TWL, "{}", buf);
        break;
    case LogLevel::Error:
        LOG_ERROR(TWL, "{}", buf);
        break;
    }
}

void SignalStop(StopReason reason, void* userdata) {
    LOG_INFO(TWL, "melonDS signaled stop, reason={}", static_cast<int>(reason));
    if (g_platform_state.stop_callback) {
        g_platform_state.stop_callback();
    }
}

void WriteNDSSave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata) {
    if (g_platform_state.nds_save_path.empty())
        return;

    std::unique_lock lock(g_platform_state.save_mutex);
    if (g_platform_state.save_data.size() != savelen) {
        g_platform_state.save_data.resize(savelen);
    }
    std::memcpy(g_platform_state.save_data.data(), savedata, savelen);

    // Write to file
    FILE* fp = fopen(g_platform_state.nds_save_path.c_str(), "wb");
    if (fp) {
        fwrite(savedata, 1, savelen, fp);
        fclose(fp);
    }
}

void WriteGBASave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata) {
    // Not used for NDS-only emulation
}

void WriteFirmware(const Firmware& firmware, u32 writeoffset, u32 writelen, void* userdata) {
    if (g_platform_state.firmware_path.empty())
        return;

    FILE* fp = fopen(g_platform_state.firmware_path.c_str(), "wb");
    if (fp) {
        fwrite(firmware.Buffer(), 1, firmware.Length(), fp);
        fclose(fp);
    }
}

void WriteDateTime(int year, int month, int day, int hour, int minute, int second, void* userdata) {
    // Not critical for emulation
}

// Threading primitives
struct ThreadImpl {
    std::thread thread;
    std::function<void()> func;
};

struct SemaphoreImpl {
    std::mutex mutex;
    std::condition_variable cv;
    int count = 0;
};

struct MutexImpl {
    std::mutex mutex;
};

Thread* Thread_Create(std::function<void()> func) {
    auto* t = new ThreadImpl();
    t->func = std::move(func);
    t->thread = std::thread([](ThreadImpl* impl) { impl->func(); }, t);
    return reinterpret_cast<Thread*>(t);
}

void Thread_Free(Thread* thread) {
    if (!thread) return;
    auto* t = reinterpret_cast<ThreadImpl*>(thread);
    if (t->thread.joinable()) t->thread.join();
    delete t;
}

void Thread_Wait(Thread* thread) {
    if (!thread) return;
    auto* t = reinterpret_cast<ThreadImpl*>(thread);
    if (t->thread.joinable()) t->thread.join();
}

Semaphore* Semaphore_Create() {
    return reinterpret_cast<Semaphore*>(new SemaphoreImpl());
}

void Semaphore_Free(Semaphore* sema) {
    delete reinterpret_cast<SemaphoreImpl*>(sema);
}

void Semaphore_Reset(Semaphore* sema) {
    auto* s = reinterpret_cast<SemaphoreImpl*>(sema);
    std::lock_guard lock(s->mutex);
    s->count = 0;
}

void Semaphore_Wait(Semaphore* sema) {
    auto* s = reinterpret_cast<SemaphoreImpl*>(sema);
    std::unique_lock lock(s->mutex);
    s->cv.wait(lock, [s]() { return s->count > 0; });
    s->count--;
}

bool Semaphore_TryWait(Semaphore* sema, int timeout_ms) {
    auto* s = reinterpret_cast<SemaphoreImpl*>(sema);
    std::unique_lock lock(s->mutex);
    if (timeout_ms == 0) {
        if (s->count > 0) { s->count--; return true; }
        return false;
    }
    if (!s->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [s]() { return s->count > 0; }))
        return false;
    s->count--;
    return true;
}

void Semaphore_Post(Semaphore* sema, int count) {
    auto* s = reinterpret_cast<SemaphoreImpl*>(sema);
    {
        std::lock_guard lock(s->mutex);
        s->count += count;
    }
    for (int i = 0; i < count; i++) s->cv.notify_one();
}

Mutex* Mutex_Create() {
    return reinterpret_cast<Mutex*>(new MutexImpl());
}

void Mutex_Free(Mutex* mutex) {
    delete reinterpret_cast<MutexImpl*>(mutex);
}

void Mutex_Lock(Mutex* mutex) {
    reinterpret_cast<MutexImpl*>(mutex)->mutex.lock();
}

void Mutex_Unlock(Mutex* mutex) {
    reinterpret_cast<MutexImpl*>(mutex)->mutex.unlock();
}

bool Mutex_TryLock(Mutex* mutex) {
    return reinterpret_cast<MutexImpl*>(mutex)->mutex.try_lock();
}

void Sleep(u64 usecs) {
    std::this_thread::sleep_for(std::chrono::microseconds(usecs));
}

u64 GetMSCount() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

u64 GetUSCount() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

int MP_SendCmd(u8* data, int len, u64 timestamp, void* userdata) { return 0; }
int MP_SendReply(u8* data, int len, u64 timestamp, u16 aid, void* userdata) { return 0; }
int MP_SendAck(u8* data, int len, u64 timestamp, void* userdata) { return 0; }
int MP_RecvHostPacket(u8* data, u64* timestamp, void* userdata) { return 0; }
u16 MP_RecvReplies(u8* data, u64 timestamp, u16 aidmask, void* userdata) { return 0; }
int MP_RecvPacket(u8* data, u64* timestamp, u16* aid, void* userdata) { return 0; }
int MP_RecvCmdPacket(u8* data, u64* timestamp, u16* aid, void* userdata) { return 0; }

void MP_Begin(void* userdata) {}
void MP_End(void* userdata) {}

int Net_SendPacket(u8* data, int len, void* userdata) {
    if (!g_platform_state.net_initialized) return 0;
    return g_platform_state.net.SendPacket(data, len, 0);
}
int Net_RecvPacket(u8* data, void* userdata) {
    if (!g_platform_state.net_initialized) return 0;
    return g_platform_state.net.RecvPacket(data, 0);
}

void Camera_Start(int num, void* userdata) {}
void Camera_Stop(int num, void* userdata) {}
void Camera_CaptureFrame(int num, u32* frame, int width, int height, bool yuv, void* userdata) {}

void Mic_Start(void* userdata) {}
void Mic_Stop(void* userdata) {}
int Mic_ReadInput(s16* data, int maxlength, void* userdata) { return 0; }

struct AACDecoder;
AACDecoder* AAC_Init() { return nullptr; }
AACDecoder* AAC_Create() { return nullptr; }
void AAC_Free(AACDecoder* dec) {}
void AAC_DeInit(AACDecoder* dec) {}
bool AAC_Configure(AACDecoder* dec, int frequency, int channels) { return false; }
bool AAC_DecodeFrame(AACDecoder* dec, const void* input, int inputlen, void* output, int outputlen) { return false; }

bool Addon_KeyDown(KeyType type, void* userdata) { return false; }
void Addon_RumbleStart(u32 len, void* userdata) {}
void Addon_RumbleStop(void* userdata) {}

struct DynamicLibrary;
DynamicLibrary* DynamicLibrary_Load(const char* lib) { return nullptr; }
void DynamicLibrary_Unload(DynamicLibrary* lib) {}
void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name) { return nullptr; }

void Speaker_ReadSamples(u16* data, u32 num_samples, void* userdata) {}
void Speaker_SetFormat(int bits, int sample_rate, void* userdata) {}
void Speaker_Stop(void* userdata) {}
void Speaker_Start(void* userdata) {}
void Speaker_WriteSamples(const s16* data, u32 num_samples, void* userdata) {}

} // namespace melonDS::Platform

// ---------------------------------------------------------------------------
// TWL::Core implementation
// ---------------------------------------------------------------------------

namespace TWL {

struct Core::Impl {
    std::unique_ptr<melonDS::NDS> nds;
    melonDS::NDSArgs nds_args;
    std::string rom_path;
    u32 output_sample_rate = 48000; // matches NDSArgs default
};

Core::Core() : impl(std::make_unique<Impl>()) {}

Core::~Core() {
    Stop();
}

bool Core::Initialize(const std::string& nds_rom_path) {
    LOG_INFO(TWL, "Initializing TWL core with ROM: {}", nds_rom_path);

    impl->rom_path = nds_rom_path;

    // Set up platform state
    g_platform_state.base_path = FileUtil::GetUserPath(FileUtil::UserPath::UserDir);
    g_platform_state.nds_save_path = nds_rom_path + ".save";
    g_platform_state.stop_callback = [this]() {
        stop_requested = true;
    };

    // Set up NDS args - FreeBIOS defaults are used if no real BIOS is available
    impl->nds_args = melonDS::NDSArgs{};

    // Load existing save data if available
    if (FileUtil::Exists(g_platform_state.nds_save_path)) {
        FILE* fp = fopen(g_platform_state.nds_save_path.c_str(), "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (size > 0 && size <= 128 * 1024) { // NDS saves max 128KB
                g_platform_state.save_data.resize(static_cast<size_t>(size));
                fread(g_platform_state.save_data.data(), 1, static_cast<size_t>(size), fp);
                LOG_INFO(TWL, "Loaded {} bytes of save data", size);
            }
            fclose(fp);
        }
    }

    // Create NDS instance
    try {
        impl->nds = std::make_unique<melonDS::NDS>(std::move(impl->nds_args));
    } catch (const std::exception& e) {
        error_message = std::string("Failed to create NDS instance: ") + e.what();
        LOG_ERROR(TWL, "{}", error_message);
        return false;
    }

    // Load the ROM
    FILE* rom_file = fopen(nds_rom_path.c_str(), "rb");
    if (!rom_file) {
        error_message = "Failed to open ROM file: " + nds_rom_path;
        LOG_ERROR(TWL, "{}", error_message);
        return false;
    }

    fseek(rom_file, 0, SEEK_END);
    long rom_size = ftell(rom_file);
    fseek(rom_file, 0, SEEK_SET);

    std::vector<u8> rom_data(static_cast<size_t>(rom_size));
    size_t read_count = fread(rom_data.data(), 1, static_cast<size_t>(rom_size), rom_file);
    fclose(rom_file);

    if (read_count != static_cast<size_t>(rom_size)) {
        error_message = "Failed to read ROM file completely";
        LOG_ERROR(TWL, "{}", error_message);
        return false;
    }

    // Parse and insert the ROM cart
    auto cart = melonDS::NDSCart::ParseROM(rom_data.data(), static_cast<u32>(rom_size));
    if (!cart) {
        error_message = "Failed to parse NDS ROM";
        LOG_ERROR(TWL, "{}", error_message);
        return false;
    }

    impl->nds->NDSCartSlot.SetCart(std::move(cart));

    // Set up networking (WFC / Wiimmfi)
    g_platform_state.net.RegisterInstance(0);
    {
        auto send_callback = [](const u8* data, int len) {
            g_platform_state.net.RXEnqueue(data, len);
        };
        auto driver = std::make_unique<melonDS::Net_Slirp>(send_callback);
        g_platform_state.net.SetDriver(std::move(driver));
    }
    g_platform_state.net_initialized = true;
    LOG_INFO(TWL, "Networking initialized (Net_Slirp / WFC)");

    LOG_INFO(TWL, "TWL core initialized successfully");
    initialized = true;
    return true;
}

void Core::Start() {
    if (!initialized || running)
        return;

    stop_requested = false;
    running = true;

    emu_thread = std::make_unique<std::thread>([this]() {
        RunLoop();
    });
}

void Core::Stop() {
    if (!running)
        return;

    stop_requested = true;
    if (emu_thread && emu_thread->joinable()) {
        emu_thread->join();
    }
    emu_thread.reset();

    // Clean up networking
    if (g_platform_state.net_initialized) {
        g_platform_state.net.SetDriver(nullptr);
        g_platform_state.net.UnregisterInstance(0);
        g_platform_state.net_initialized = false;
    }
}

void Core::RunLoop() {
    LOG_INFO(TWL, "TWL emulation loop started");

    // Sync to ~60fps: NDS runs at 59.826 Hz
    static constexpr auto frame_duration = std::chrono::microseconds(16739); // ~59.826 Hz

    while (!stop_requested && running) {
        auto frame_start = std::chrono::steady_clock::now();

        // Update input
        UpdateInput();

        // Run one frame
        impl->nds->RunFrame();

        // Copy screen output
        CopyScreens();

        // Forward audio output to callback
        if (audio_callback) {
            s16 audio_buf[2048 * 2]; // stereo, up to 2048 samples per frame
            int samples_read = impl->nds->SPU.ReadOutput(audio_buf, 2048);
            if (samples_read > 0) {
                audio_callback(audio_buf, static_cast<std::size_t>(samples_read),
                               impl->output_sample_rate);
            }
        }

        // Check if NDS wants to stop
        if (impl->nds->HaltInterrupted(0) || stop_requested) {
            LOG_INFO(TWL, "NDS halted or stop requested");
            break;
        }

        // Frame timing: sleep for remaining time to hit 60fps
        auto elapsed = std::chrono::steady_clock::now() - frame_start;
        if (elapsed < frame_duration) {
            std::this_thread::sleep_for(frame_duration - elapsed);
        }
    }

    running = false;
    LOG_INFO(TWL, "TWL emulation loop ended");
}

void Core::CopyScreens() {
    void* top = nullptr;
    void* bottom = nullptr;

    if (impl->nds->GPU.GetFramebuffers(&top, &bottom)) {
        if (top) {
            std::memcpy(top_screen.data(), top,
                       DS_SCREEN_WIDTH * DS_SCREEN_HEIGHT * sizeof(u32));
        }
        if (bottom) {
            std::memcpy(bottom_screen.data(), bottom,
                       DS_SCREEN_WIDTH * DS_SCREEN_HEIGHT * sizeof(u32));
        }
    }
}

void Core::UpdateInput() {
    u16 btns = button_state.load();
    TouchState touch = touch_state;

    // melonDS input is directly set on the NDS
    impl->nds->SetKeyMask(static_cast<u32>(btns));

    if (touch.pressed) {
        impl->nds->TouchScreen(touch.x, touch.y);
    } else {
        impl->nds->ReleaseScreen();
    }
}

std::string Core::GetROMTitle() const {
    if (!initialized || !impl->nds || !impl->nds->NDSCartSlot.CartInserted())
        return {};

    const auto& header = impl->nds->NDSCartSlot.GetCart()->GetHeader();
    return std::string(header.GameTitle, strnlen(header.GameTitle, sizeof(header.GameTitle)));
}

std::string Core::GetGameCode() const {
    if (!initialized || !impl->nds || !impl->nds->NDSCartSlot.CartInserted())
        return {};

    const auto& header = impl->nds->NDSCartSlot.GetCart()->GetHeader();
    return std::string(header.GameCode, strnlen(header.GameCode, sizeof(header.GameCode)));
}

u16 Core::Map3DSButtonsToNDS(u32 hbl_button_state, s16 circle_pad_x, s16 circle_pad_y) {
    u16 nds_buttons = 0;

    if (hbl_button_state & (1 << 700)) nds_buttons |= TWL::ButtonA;
    if (hbl_button_state & (1 << 701)) nds_buttons |= TWL::ButtonB;
    if (hbl_button_state & (1 << 705)) nds_buttons |= TWL::ButtonSelect;
    if (hbl_button_state & (1 << 704)) nds_buttons |= TWL::ButtonStart;
    if (hbl_button_state & (1 << 712)) nds_buttons |= TWL::ButtonRight;
    if (hbl_button_state & (1 << 711)) nds_buttons |= TWL::ButtonLeft;
    if (hbl_button_state & (1 << 709)) nds_buttons |= TWL::ButtonUp;
    if (hbl_button_state & (1 << 710)) nds_buttons |= TWL::ButtonDown;
    if (hbl_button_state & (1 << 774)) nds_buttons |= TWL::ButtonR;
    if (hbl_button_state & (1 << 773)) nds_buttons |= TWL::ButtonL;
    if (hbl_button_state & (1 << 702)) nds_buttons |= TWL::ButtonX;
    if (hbl_button_state & (1 << 703)) nds_buttons |= TWL::ButtonY;

    // Map CirclePad to DPad if pressed enough (threshold ~40)
    if (std::abs(circle_pad_x) > 40) {
        if (circle_pad_x > 0) nds_buttons |= TWL::ButtonRight;
        else nds_buttons |= TWL::ButtonLeft;
    }
    if (std::abs(circle_pad_y) > 40) {
        if (circle_pad_y > 0) nds_buttons |= TWL::ButtonUp;
        else nds_buttons |= TWL::ButtonDown;
    }

    return nds_buttons;
}

void Core::SetButtonState(u16 buttons) {
    button_state.store(buttons);
}

void Core::SetTouchState(const TouchState& touch) {
    touch_state = touch;
}

void Core::SetAudioCallback(std::function<void(const s16* samples, std::size_t num_samples, u32 sample_rate)> cb) {
    audio_callback = std::move(cb);
}

int Core::ReadAudioOutput(s16* out_buf, int max_samples) {
    if (!initialized || !impl->nds) return 0;
    return impl->nds->SPU.ReadOutput(out_buf, max_samples);
}

u32 Core::GetAudioSampleRate() const {
    return impl ? impl->output_sample_rate : 48000;
}

bool Core::InsertGBACart(const std::string& gba_rom_path, const std::string& gba_save_path) {
    if (!initialized || !impl->nds) return false;

    FILE* f = fopen(gba_rom_path.c_str(), "rb");
    if (!f) {
        LOG_ERROR(TWL, "Failed to open GBA ROM: {}", gba_rom_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<u8> rom_data(static_cast<size_t>(size));
    size_t read_count = fread(rom_data.data(), 1, static_cast<size_t>(size), f);
    fclose(f);

    if (read_count != static_cast<size_t>(size)) {
        LOG_ERROR(TWL, "Failed to read GBA ROM completely");
        return false;
    }

    // Load save data if provided
    std::unique_ptr<u8[]> sram_data;
    u32 sram_len = 0;
    if (!gba_save_path.empty() && FileUtil::Exists(gba_save_path)) {
        FILE* sf = fopen(gba_save_path.c_str(), "rb");
        if (sf) {
            fseek(sf, 0, SEEK_END);
            sram_len = static_cast<u32>(ftell(sf));
            fseek(sf, 0, SEEK_SET);
            sram_data = std::make_unique<u8[]>(sram_len);
            fread(sram_data.get(), 1, sram_len, sf);
            fclose(sf);
            LOG_INFO(TWL, "Loaded {} bytes of GBA save data", sram_len);
        }
    }

    auto cart = melonDS::GBACart::ParseROM(
        reinterpret_cast<const u8*>(rom_data.data()), static_cast<u32>(size),
        sram_data.get(), sram_len);

    if (!cart) {
        LOG_ERROR(TWL, "Failed to parse GBA ROM");
        return false;
    }

    impl->nds->SetGBACart(std::move(cart));
    LOG_INFO(TWL, "GBA cart inserted: {}", gba_rom_path);
    return true;
}

bool Core::LoadDSSave(const std::string& save_path) {
    if (!initialized || !impl->nds || !impl->nds->NDSCartSlot.CartInserted()) return false;

    FILE* f = fopen(save_path.c_str(), "rb");
    if (!f) {
        LOG_ERROR(TWL, "Failed to open DS save: {}", save_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 128 * 1024 * 1024) {
        fclose(f);
        LOG_ERROR(TWL, "Invalid DS save size: {}", size);
        return false;
    }

    std::vector<u8> data(static_cast<size_t>(size));
    fread(data.data(), 1, static_cast<size_t>(size), f);
    fclose(f);

    // Write to the expected save path so melonDS can pick it up
    g_platform_state.nds_save_path = save_path;
    g_platform_state.save_data = std::move(data);
    LOG_INFO(TWL, "DS save loaded: {} ({} bytes)", save_path, size);
    return true;
}

bool Core::SaveDSSave(const std::string& save_path) {
    if (!initialized || !impl->nds) return false;

    FILE* f = fopen(save_path.c_str(), "wb");
    if (!f) {
        LOG_ERROR(TWL, "Failed to write DS save: {}", save_path);
        return false;
    }

    std::unique_lock lock(g_platform_state.save_mutex);
    if (!g_platform_state.save_data.empty()) {
        fwrite(g_platform_state.save_data.data(), 1, g_platform_state.save_data.size(), f);
        LOG_INFO(TWL, "DS save written: {} ({} bytes)", save_path, g_platform_state.save_data.size());
    }
    fclose(f);
    return true;
}

bool Core::InitializeDSFirmware() {
    LOG_INFO(TWL, "Initializing DS firmware boot (NDS mode)");

    // Set up platform state
    g_platform_state.base_path = FileUtil::GetUserPath(FileUtil::UserPath::UserDir);
    g_platform_state.stop_callback = [this]() {
        stop_requested = true;
    };

    // Use NDS BIOS files if available, otherwise FreeBIOS
    impl->nds_args = melonDS::NDSArgs{};

    // Load real NDS BIOS if available
    std::string bios9_path = g_platform_state.base_path + "/melonDS/bios9.bin";
    std::string bios7_path = g_platform_state.base_path + "/melonDS/bios7.bin";

    if (FileUtil::Exists(bios9_path)) {
        auto bios9 = std::make_unique<melonDS::ARM9BIOSImage>();
        FILE* f = fopen(bios9_path.c_str(), "rb");
        if (f) {
            fread(bios9->data(), 1, bios9->size(), f);
            fclose(f);
            impl->nds_args.ARM9BIOS = std::move(bios9);
            LOG_INFO(TWL, "Loaded NDS ARM9 BIOS: {}", bios9_path);
        }
    }

    if (FileUtil::Exists(bios7_path)) {
        auto bios7 = std::make_unique<melonDS::ARM7BIOSImage>();
        FILE* f = fopen(bios7_path.c_str(), "rb");
        if (f) {
            fread(bios7->data(), 1, bios7->size(), f);
            fclose(f);
            impl->nds_args.ARM7BIOS = std::move(bios7);
            LOG_INFO(TWL, "Loaded NDS ARM7 BIOS: {}", bios7_path);
        }
    }

    // Create NDS instance
    try {
        impl->nds = std::make_unique<melonDS::NDS>(std::move(impl->nds_args));
    } catch (const std::exception& e) {
        error_message = std::string("Failed to create NDS instance: ") + e.what();
        LOG_ERROR(TWL, "{}", error_message);
        return false;
    }

    // No ROM inserted - boot firmware directly
    dsi_mode = false;

    // Set up networking
    g_platform_state.net.RegisterInstance(0);
    {
        auto send_callback = [](const u8* data, int len) {
            g_platform_state.net.RXEnqueue(data, len);
        };
        auto driver = std::make_unique<melonDS::Net_Slirp>(send_callback);
        g_platform_state.net.SetDriver(std::move(driver));
    }
    g_platform_state.net_initialized = true;

    LOG_INFO(TWL, "DS firmware boot initialized successfully");
    initialized = true;
    return true;
}

bool Core::InitializeDSiNAND(const std::string& nand_path) {
    LOG_INFO(TWL, "Initializing DSi NAND boot: {}", nand_path);

    if (!FileUtil::Exists(nand_path)) {
        error_message = "DSi NAND file not found: " + nand_path;
        LOG_ERROR(TWL, "{}", error_message);
        return false;
    }

    // Set up platform state
    g_platform_state.base_path = FileUtil::GetUserPath(FileUtil::UserPath::UserDir);
    g_platform_state.stop_callback = [this]() {
        stop_requested = true;
    };

    // Open the NAND file
    melonDS::Platform::FileHandle* nand_file = melonDS::Platform::OpenFile(nand_path, melonDS::Platform::FileMode::Read);
    if (!nand_file) {
        error_message = "Failed to open DSi NAND: " + nand_path;
        LOG_ERROR(TWL, "{}", error_message);
        return false;
    }

    // The NAND image needs the eMMC CID for crypto.
    // Read the nocash footer to get it.
    melonDS::Platform::FileSeek(nand_file, -0x40, melonDS::Platform::FileSeekOrigin::End);
    char footer_check[16];
    melonDS::Platform::FileRead(footer_check, 1, sizeof(footer_check), nand_file);

    melonDS::DSi_NAND::DSiKey es_keyY{};
    if (memcmp(footer_check, "DSi eMMC CID/CPU", 16) == 0) {
        melonDS::Platform::FileRead(es_keyY.data(), 1, sizeof(es_keyY), nand_file);
        LOG_INFO(TWL, "Read eMMC CID from nocash footer");
    } else {
        // Try the alternate footer location
        melonDS::Platform::FileSeek(nand_file, 0x000FF800, melonDS::Platform::FileSeekOrigin::Start);
        melonDS::Platform::FileRead(footer_check, 1, sizeof(footer_check), nand_file);
        if (memcmp(footer_check, "DSi eMMC CID/CPU", 16) == 0) {
            melonDS::Platform::FileRead(es_keyY.data(), 1, sizeof(es_keyY), nand_file);
            LOG_INFO(TWL, "Read eMMC CID from alternate footer location");
        } else {
            melonDS::Platform::CloseFile(nand_file);
            error_message = "DSi NAND missing nocash footer";
            LOG_ERROR(TWL, "{}", error_message);
            return false;
        }
    }

    // Create NAND image
    melonDS::DSi_NAND::NANDImage nand_img(nand_file, es_keyY);
    if (!nand_img) {
        melonDS::Platform::CloseFile(nand_file);
        error_message = "Failed to initialize DSi NAND crypto";
        LOG_ERROR(TWL, "{}", error_message);
        return false;
    }

    // Set up DSi args
    auto dsi_args = std::make_unique<melonDS::DSiArgs>();
    dsi_args->NANDImage = std::move(nand_img);

    // Load DSi BIOS files if available
    std::string arm9i_path = g_platform_state.base_path + "/melonDS/bios9i.bin";
    std::string arm7i_path = g_platform_state.base_path + "/melonDS/bios7i.bin";
    std::string arm9_path = g_platform_state.base_path + "/melonDS/bios9.bin";
    std::string arm7_path = g_platform_state.base_path + "/melonDS/bios7.bin";

    if (FileUtil::Exists(arm9i_path)) {
        auto bios = std::make_unique<melonDS::DSiBIOSImage>();
        FILE* f = fopen(arm9i_path.c_str(), "rb");
        if (f) { fread(bios->data(), 1, bios->size(), f); fclose(f); }
        dsi_args->ARM9iBIOS = std::move(bios);
    }
    if (FileUtil::Exists(arm7i_path)) {
        auto bios = std::make_unique<melonDS::DSiBIOSImage>();
        FILE* f = fopen(arm7i_path.c_str(), "rb");
        if (f) { fread(bios->data(), 1, bios->size(), f); fclose(f); }
        dsi_args->ARM7iBIOS = std::move(bios);
    }
    if (FileUtil::Exists(arm9_path)) {
        auto bios = std::make_unique<melonDS::ARM9BIOSImage>();
        FILE* f = fopen(arm9_path.c_str(), "rb");
        if (f) { fread(bios->data(), 1, bios->size(), f); fclose(f); }
        dsi_args->ARM9BIOS = std::move(bios);
    }
    if (FileUtil::Exists(arm7_path)) {
        auto bios = std::make_unique<melonDS::ARM7BIOSImage>();
        FILE* f = fopen(arm7_path.c_str(), "rb");
        if (f) { fread(bios->data(), 1, bios->size(), f); fclose(f); }
        dsi_args->ARM7BIOS = std::move(bios);
    }

    // Create DSi instance
    try {
        impl->nds = std::make_unique<melonDS::DSi>(std::move(*dsi_args));
    } catch (const std::exception& e) {
        error_message = std::string("Failed to create DSi instance: ") + e.what();
        LOG_ERROR(TWL, "{}", error_message);
        return false;
    }

    dsi_mode = true;

    // Set up networking
    g_platform_state.net.RegisterInstance(0);
    {
        auto send_callback = [](const u8* data, int len) {
            g_platform_state.net.RXEnqueue(data, len);
        };
        auto driver = std::make_unique<melonDS::Net_Slirp>(send_callback);
        g_platform_state.net.SetDriver(std::move(driver));
    }
    g_platform_state.net_initialized = true;

    LOG_INFO(TWL, "DSi NAND boot initialized successfully");
    initialized = true;
    return true;
}

std::vector<std::string> Core::GetDSiNANDTitles(const std::string& nand_path) {
    std::vector<std::string> titles;

    if (!FileUtil::Exists(nand_path))
        return titles;

    melonDS::Platform::FileHandle* nand_file = melonDS::Platform::OpenFile(nand_path, melonDS::Platform::FileMode::Read);
    if (!nand_file)
        return titles;

    // Read nocash footer to get eMMC CID
    melonDS::Platform::FileSeek(nand_file, -0x40, melonDS::Platform::FileSeekOrigin::End);
    char footer_check[16];
    melonDS::Platform::FileRead(footer_check, 1, sizeof(footer_check), nand_file);

    melonDS::DSi_NAND::DSiKey es_keyY{};
    if (memcmp(footer_check, "DSi eMMC CID/CPU", 16) == 0) {
        melonDS::Platform::FileRead(es_keyY.data(), 1, sizeof(es_keyY), nand_file);
    } else {
        melonDS::Platform::FileSeek(nand_file, 0x000FF800, melonDS::Platform::FileSeekOrigin::Start);
        melonDS::Platform::FileRead(footer_check, 1, sizeof(footer_check), nand_file);
        if (memcmp(footer_check, "DSi eMMC CID/CPU", 16) == 0) {
            melonDS::Platform::FileRead(es_keyY.data(), 1, sizeof(es_keyY), nand_file);
        } else {
            melonDS::Platform::CloseFile(nand_file);
            return titles;
        }
    }

    melonDS::DSi_NAND::NANDImage nand_img(nand_file, es_keyY);
    if (!nand_img) {
        melonDS::Platform::CloseFile(nand_file);
        return titles;
    }

    melonDS::DSi_NAND::NANDMount mount(nand_img);
    if (!mount) {
        return titles;
    }

    // List titles in category 0x0003 (DSi system apps) and 0x0001 (user apps)
    std::vector<u32> title_list;
    mount.ListTitles(0x0003, title_list); // DSi system titles
    mount.ListTitles(0x0001, title_list); // User titles

    for (u32 titleid : title_list) {
        melonDS::NDSHeader header{};
        melonDS::NDSBanner banner{};
        u32 version = 0;
        mount.GetTitleInfo(0x0003, titleid, version, &header, &banner);

        // Extract game title from banner (English preferred, fallback to header)
        std::string title_str;
        if (banner.EnglishTitle[0]) {
            for (int i = 0; i < 128 && banner.EnglishTitle[i]; i++) {
                char c = static_cast<char>(banner.EnglishTitle[i]);
                if (c >= 0x20) title_str += c;
            }
        } else {
            title_str = std::string(header.GameTitle, strnlen(header.GameTitle, sizeof(header.GameTitle)));
        }

        if (!title_str.empty()) {
            char id_buf[16]{};
            snprintf(id_buf, sizeof(id_buf), "[%08X]", titleid);
            titles.push_back(std::string(id_buf) + " " + title_str);
        }
    }

    return titles;
}

} // namespace TWL
