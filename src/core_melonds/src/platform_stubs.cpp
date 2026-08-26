// Default weak implementations of melonDS Platform functions.
// These are overridden by the stronger definitions in twl_core.cpp (citra_core).
// Without these, the melonDS core static library has unresolved Platform symbols.

#include <cstddef>
#include <cstdint>
#include <functional>
#include "Platform.h"

using namespace melonDS;

// Use weak attribute so strong definitions in twl_core.cpp override these.
#if defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif

namespace melonDS::Platform {

WEAK std::string GetLocalFilePath(const std::string& filename) { return filename; }

WEAK FileHandle* OpenFile(const std::string& path, FileMode mode) { return nullptr; }
WEAK FileHandle* OpenLocalFile(const std::string& path, FileMode mode) { return nullptr; }
WEAK bool FileExists(const std::string& name) { return false; }
WEAK bool LocalFileExists(const std::string& name) { return false; }
WEAK bool CheckFileWritable(const std::string& filepath) { return false; }
WEAK bool CheckLocalFileWritable(const std::string& filepath) { return false; }
WEAK bool CloseFile(FileHandle* file) { return false; }
WEAK bool IsEndOfFile(FileHandle* file) { return true; }
WEAK bool FileReadLine(char* str, int count, FileHandle* file) { return false; }
WEAK u64 FilePosition(FileHandle* file) { return 0; }
WEAK bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin) { return false; }
WEAK void FileRewind(FileHandle* file) {}
WEAK u64 FileRead(void* data, u64 size, u64 count, FileHandle* file) { return 0; }
WEAK bool FileFlush(FileHandle* file) { return false; }
WEAK u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file) { return 0; }
WEAK u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...) { return 0; }
WEAK u64 FileLength(FileHandle* file) { return 0; }

WEAK void Log(LogLevel level, const char* fmt, ...) {}

WEAK void SignalStop(StopReason reason, void* userdata) {}
WEAK void WriteNDSSave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata) {}
WEAK void WriteGBASave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata) {}
WEAK void WriteFirmware(const Firmware& firmware, u32 writeoffset, u32 writelen, void* userdata) {}
WEAK void WriteDateTime(int year, int month, int day, int hour, int minute, int second, void* userdata) {}

WEAK Thread* Thread_Create(std::function<void()> func) { return nullptr; }
WEAK void Thread_Free(Thread* thread) {}
WEAK void Thread_Wait(Thread* thread) {}

WEAK Semaphore* Semaphore_Create() { return nullptr; }
WEAK void Semaphore_Free(Semaphore* sema) {}
WEAK void Semaphore_Reset(Semaphore* sema) {}
WEAK void Semaphore_Wait(Semaphore* sema) {}
WEAK bool Semaphore_TryWait(Semaphore* sema, int timeout_ms) { return false; }
WEAK void Semaphore_Post(Semaphore* sema, int count) {}

WEAK Mutex* Mutex_Create() { return nullptr; }
WEAK void Mutex_Free(Mutex* mutex) {}
WEAK void Mutex_Lock(Mutex* mutex) {}
WEAK void Mutex_Unlock(Mutex* mutex) {}
WEAK bool Mutex_TryLock(Mutex* mutex) { return false; }

WEAK void Sleep(u64 usecs) {}
WEAK u64 GetMSCount() { return 0; }
WEAK u64 GetUSCount() { return 0; }

WEAK int MP_SendCmd(u8* data, int len, u64 timestamp, void* userdata) { return 0; }
WEAK int MP_SendReply(u8* data, int len, u64 timestamp, u16 aid, void* userdata) { return 0; }
WEAK int MP_SendAck(u8* data, int len, u64 timestamp, void* userdata) { return 0; }
WEAK int MP_RecvHostPacket(u8* data, u64* timestamp, void* userdata) { return 0; }
WEAK u16 MP_RecvReplies(u8* data, u64 timestamp, u16 aidmask, void* userdata) { return 0; }
WEAK int MP_RecvPacket(u8* data, u64* timestamp, u16* aid, void* userdata) { return 0; }
WEAK int MP_RecvCmdPacket(u8* data, u64* timestamp, u16* aid, void* userdata) { return 0; }
WEAK void MP_Begin(void* userdata) {}
WEAK void MP_End(void* userdata) {}

WEAK int Net_SendPacket(u8* data, int len, void* userdata) { return 0; }
WEAK int Net_RecvPacket(u8* data, void* userdata) { return 0; }

WEAK void Camera_Start(int num, void* userdata) {}
WEAK void Camera_Stop(int num, void* userdata) {}
WEAK void Camera_CaptureFrame(int num, u32* frame, int width, int height, bool yuv, void* userdata) {}

WEAK void Mic_Start(void* userdata) {}
WEAK void Mic_Stop(void* userdata) {}
WEAK int Mic_ReadInput(s16* data, int maxlength, void* userdata) { return 0; }

struct AACDecoder;
WEAK AACDecoder* AAC_Init() { return nullptr; }
WEAK void AAC_Free(AACDecoder* dec) {}
WEAK void AAC_DeInit(AACDecoder* dec) {}
WEAK bool AAC_Configure(AACDecoder* dec, int frequency, int channels) { return false; }
WEAK bool AAC_DecodeFrame(AACDecoder* dec, const void* input, int inputlen, void* output, int outputlen) { return false; }

WEAK bool Addon_KeyDown(KeyType type, void* userdata) { return false; }
WEAK void Addon_RumbleStart(u32 len, void* userdata) {}
WEAK void Addon_RumbleStop(void* userdata) {}
WEAK float Addon_MotionQuery(MotionQueryType type, void* userdata) { return 0.0f; }

struct DynamicLibrary;
WEAK DynamicLibrary* DynamicLibrary_Load(const char* lib) { return nullptr; }
WEAK void DynamicLibrary_Unload(DynamicLibrary* lib) {}
WEAK void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name) { return nullptr; }

WEAK void Speaker_ReadSamples(u16* data, u32 num_samples, void* userdata) {}
WEAK void Speaker_SetFormat(int bits, int sample_rate, void* userdata) {}
WEAK void Speaker_Stop(void* userdata) {}
WEAK void Speaker_Start(void* userdata) {}
WEAK void Speaker_WriteSamples(const s16* data, u32 num_samples, void* userdata) {}

} // namespace melonDS::Platform
