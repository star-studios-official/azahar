// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio_core/audio_types.h"
#include "common/alignment.h"
#include "common/archives.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/result.h"
#include "core/hle/service/csnd/csnd_snd.h"

SERVICE_CONSTRUCT_IMPL(Service::CSND::CSND_SND)
SERIALIZE_EXPORT_IMPL(Service::CSND::CSND_SND)

namespace Service::CSND {

namespace {

/// The CSND sample-rate timer is derived from this clock (see nn::csnd::CalculateTimer).
constexpr double CsndSystemClock = 67027964.0;
/// The output rate of the CSND hardware (matches the emulator's audio frame rate).
constexpr double CsndOutputRate = 32728.0;
/// Maximum number of bytes decoded for a single block, to guard against absurd sizes.
constexpr u32 MaxDecodedBytes = 1 << 24;

/// Standard IMA-ADPCM tables, used by the CSND hardware for CWAV playback.
constexpr std::array<int, 16> ima_index_table{
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};
constexpr std::array<int, 89> ima_step_table{
    7,    8,    9,    10,   11,   12,   13,   14,   16,   17,   19,   21,   23,   25,   28,   31,
    34,   37,   41,   45,   50,   55,   60,   66,   73,   80,   88,   97,   107,  118,  130,  143,
    157,  173,  190,  209,  230,  253,  279,  307,  337,  370,  408,  449,  494,  544,  598,  658,
    724,  796,  876,  963,  1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
    13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

/// Resampling step (in source samples per output sample) for a CSND channel.
double GetChannelStep(const Channel& channel) {
    if (channel.sample_rate == 0) {
        return 1.0;
    }
    // The stored "sample rate" is actually the hardware timer value: timer = 67027964 / rate.
    const double source_rate = CsndSystemClock / channel.sample_rate;
    return source_rate / CsndOutputRate;
}

} // namespace

enum class CommandId : u16 {
    Start = 0x000,
    Pause = 0x001,
    SetEncoding = 0x002,
    SetSecondBlock = 0x003,
    SetLoopMode = 0x004,
    // unknown = 0x005,
    SetLinearInterpolation = 0x006,
    SetPsgDuty = 0x007,
    SetSampleRate = 0x008,
    SetVolume = 0x009,
    SetFirstBlock = 0x00A,
    SetFirstBlockAdpcmState = 0x00B,
    SetSecondBlockAdpcmState = 0x00C,
    SetSecondBlockAdpcmReload = 0x00D,
    ConfigureChannel = 0x00E,
    ConfigurePsg = 0x00F,
    ConfigurePsgNoise = 0x010,
    // 0x10x commands are audio capture related
    // unknown = 0x200
    UpdateState = 0x300,
};

struct Type0Command {
    u16_le next_command_offset;
    enum_le<CommandId> command_id;
    u8 finished;
    INSERT_PADDING_BYTES(3);
    union {
        struct {
            u32_le channel;
            u32_le value;
            INSERT_PADDING_BYTES(0x10);
        } start;

        struct {
            u32_le channel;
            u32_le value;
            INSERT_PADDING_BYTES(0x10);
        } pause;

        struct {
            u32_le channel;
            Encoding value;
            INSERT_PADDING_BYTES(0x13);
        } set_encoding;

        struct {
            u32_le channel;
            LoopMode value;
            INSERT_PADDING_BYTES(0x13);
        } set_loop_mode;

        struct {
            u32_le channel;
            u32_le value;
            INSERT_PADDING_BYTES(0x10);
        } set_linear_interpolation;

        struct {
            u32_le channel;
            u8 value;
            INSERT_PADDING_BYTES(0x13);
        } set_psg_duty;

        struct {
            u32_le channel;
            u32_le value;
            INSERT_PADDING_BYTES(0x10);
        } set_sample_rate;

        struct {
            u32_le channel;
            u16_le left_channel_volume;
            u16_le right_channel_volume;
            u16_le left_capture_volume;
            u16_le right_capture_volume;
            INSERT_PADDING_BYTES(0xC);
        } set_volume;

        struct {
            u32_le channel;
            u32_le address;
            u32_le size;
            INSERT_PADDING_BYTES(0xC);
        } set_block; // for either first block or second block

        struct {
            u32_le channel;
            s16_le predictor;
            u8 step_index;
            INSERT_PADDING_BYTES(0x11);
        } set_adpcm_state; // for either first block or second block

        struct {
            u32_le channel;
            u8 value;
            INSERT_PADDING_BYTES(0x13);
        } set_second_block_adpcm_reload;

        struct {
            union {
                BitField<0, 6, u32> channel;
                BitField<6, 1, u32> linear_interpolation;

                BitField<10, 2, u32> loop_mode;
                BitField<12, 2, u32> encoding;
                BitField<14, 1, u32> enable_playback;

                BitField<16, 16, u32> sample_rate;
            };

            u16_le left_channel_volume;
            u16_le right_channel_volume;
            u16_le left_capture_volume;
            u16_le right_capture_volume;
            u32_le block1_address;
            u32_le block2_address;
            u32_le size;
        } configure_channel;

        struct {
            union {
                BitField<0, 6, u32> channel;
                BitField<14, 1, u32> enable_playback;
                BitField<16, 16, u32> sample_rate;
            };
            u16_le left_channel_volume;
            u16_le right_channel_volume;
            u16_le left_capture_volume;
            u16_le right_capture_volume;
            u32_le duty;
            INSERT_PADDING_BYTES(0x8);
        } configure_psg;

        struct {
            union {
                BitField<0, 6, u32> channel;
                BitField<14, 1, u32> enable_playback;
            };
            u16_le left_channel_volume;
            u16_le right_channel_volume;
            u16_le left_capture_volume;
            u16_le right_capture_volume;
            INSERT_PADDING_BYTES(0xC);
        } configure_psg_noise;
    };
};
static_assert(sizeof(Type0Command) == 0x20, "Type0Command structure size is wrong");

struct MasterState {
    u32_le unknown_channel_flag;
    u32_le unknown;
};
static_assert(sizeof(MasterState) == 0x8, "MasterState structure size is wrong");

struct ChannelState {
    u8 active;
    INSERT_PADDING_BYTES(0x3);
    s16_le adpcm_predictor;
    u8 adpcm_step_index;
    INSERT_PADDING_BYTES(0x1);

    // 3dbrew says this is the current physical address. However the assembly of CSND module
    // from 11.3 system shows this is simply assigned as 0, which is also documented on ctrulib.
    u32_le zero;
};
static_assert(sizeof(ChannelState) == 0xC, "ChannelState structure size is wrong");

struct CaptureState {
    u8 active;
    INSERT_PADDING_BYTES(0x3);
    u32_le zero;
};
static_assert(sizeof(CaptureState) == 0x8, "CaptureState structure size is wrong");

void CSND_SND::Initialize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 size = Common::AlignUp(rp.Pop<u32>(), Memory::CITRA_PAGE_SIZE);
    master_state_offset = rp.Pop<u32>();
    channel_state_offset = rp.Pop<u32>();
    capture_state_offset = rp.Pop<u32>();
    type1_command_offset = rp.Pop<u32>();

    using Kernel::MemoryPermission;
    mutex = system.Kernel().CreateMutex(false, "CSND:mutex");
    shared_memory = system.Kernel()
                        .CreateSharedMemory(nullptr, size, MemoryPermission::ReadWrite,
                                            MemoryPermission::ReadWrite, 0,
                                            Kernel::MemoryRegion::BASE, "CSND:SharedMemory")
                        .Unwrap();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 3);
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(mutex, shared_memory);

    LOG_WARNING(Service_CSND,
                "(STUBBED) called, size=0x{:08X} "
                "master_state_offset=0x{:08X} channel_state_offset=0x{:08X} "
                "capture_state_offset=0x{:08X} type1_command_offset=0x{:08X}",
                size, master_state_offset, channel_state_offset, capture_state_offset,
                type1_command_offset);
}

void CSND_SND::Shutdown(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    for (auto& channel : channels) {
        channel.playing = false;
        channel.position = 0.0;
    }
    if (mutex)
        mutex = nullptr;
    if (shared_memory)
        shared_memory = nullptr;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_WARNING(Service_CSND, "(STUBBED) called");
}

void CSND_SND::ExecuteCommands(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 addr = rp.Pop<u32>();
    LOG_DEBUG(Service_CSND, "(STUBBED) called, addr=0x{:08X}", addr);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    if (!shared_memory) {
        rb.Push(Result(ErrorDescription::InvalidResultValue, ErrorModule::CSND,
                       ErrorSummary::InvalidState, ErrorLevel::Status));
        LOG_ERROR(Service_CSND, "called, shared memory not allocated");
        return;
    }

    u32 offset = addr;
    while (offset != 0xFFFF) {
        Type0Command command;
        u8* ptr = shared_memory->GetPointer(offset);
        std::memcpy(&command, ptr, sizeof(Type0Command));
        offset = command.next_command_offset;

        switch (command.command_id) {
        case CommandId::Start:
            if (command.start.value != 0) {
                StartChannel(command.start.channel);
            } else {
                channels[command.start.channel].playing = false;
                channels[command.start.channel].position = 0.0;
            }
            break;
        case CommandId::Pause:
            channels[command.pause.channel].playing = command.pause.value != 0;
            break;
        case CommandId::SetEncoding:
            channels[command.set_encoding.channel].encoding = command.set_encoding.value;
            break;
        case CommandId::SetSecondBlock:
            channels[command.set_block.channel].block2_address = command.set_block.address;
            channels[command.set_block.channel].block2_size = command.set_block.size;
            break;
        case CommandId::SetLoopMode:
            channels[command.set_loop_mode.channel].loop_mode = command.set_loop_mode.value;
            break;
        case CommandId::SetLinearInterpolation:
            channels[command.set_linear_interpolation.channel].linear_interpolation =
                command.set_linear_interpolation.value != 0;
            break;
        case CommandId::SetPsgDuty:
            channels[command.set_psg_duty.channel].psg_duty = command.set_psg_duty.value;
            break;
        case CommandId::SetSampleRate:
            channels[command.set_sample_rate.channel].sample_rate = command.set_sample_rate.value;
            break;
        case CommandId::SetVolume:
            channels[command.set_volume.channel].left_channel_volume =
                command.set_volume.left_channel_volume;
            channels[command.set_volume.channel].right_channel_volume =
                command.set_volume.right_channel_volume;
            channels[command.set_volume.channel].left_capture_volume =
                command.set_volume.left_capture_volume;
            channels[command.set_volume.channel].right_capture_volume =
                command.set_volume.right_capture_volume;
            break;
        case CommandId::SetFirstBlock:
            channels[command.set_block.channel].block1_address = command.set_block.address;
            channels[command.set_block.channel].block1_size = command.set_block.size;
            break;
        case CommandId::SetFirstBlockAdpcmState:
            channels[command.set_adpcm_state.channel].block1_adpcm_state = {
                command.set_adpcm_state.predictor, command.set_adpcm_state.step_index};
            channels[command.set_adpcm_state.channel].block2_adpcm_state = {};
            channels[command.set_adpcm_state.channel].block2_adpcm_reload = false;
            break;
        case CommandId::SetSecondBlockAdpcmState:
            channels[command.set_adpcm_state.channel].block2_adpcm_state = {
                command.set_adpcm_state.predictor, command.set_adpcm_state.step_index};
            channels[command.set_adpcm_state.channel].block2_adpcm_reload = true;
            break;
        case CommandId::SetSecondBlockAdpcmReload:
            channels[command.set_second_block_adpcm_reload.channel].block2_adpcm_reload =
                command.set_second_block_adpcm_reload.value != 0;
            break;
        case CommandId::ConfigureChannel: {
            auto& configure = command.configure_channel;
            auto& channel = channels[configure.channel];
            channel.linear_interpolation = configure.linear_interpolation != 0;
            channel.loop_mode = static_cast<LoopMode>(configure.loop_mode.Value());
            channel.encoding = static_cast<Encoding>(configure.encoding.Value());
            channel.sample_rate = configure.sample_rate;
            channel.left_channel_volume = configure.left_channel_volume;
            channel.right_channel_volume = configure.right_channel_volume;
            channel.left_capture_volume = configure.left_capture_volume;
            channel.right_capture_volume = configure.right_capture_volume;
            channel.block1_address = configure.block1_address;
            channel.block2_address = configure.block2_address;
            channel.block1_size = channel.block2_size = configure.size;
            if (configure.enable_playback) {
                StartChannel(configure.channel);
            }
            break;
        }
        case CommandId::ConfigurePsg: {
            auto& configure = command.configure_psg;
            auto& channel = channels[configure.channel];
            channel.encoding = Encoding::Psg;
            channel.is_noise = false;
            channel.psg_duty = configure.duty;
            channel.sample_rate = configure.sample_rate;
            channel.left_channel_volume = configure.left_channel_volume;
            channel.right_channel_volume = configure.right_channel_volume;
            channel.left_capture_volume = configure.left_capture_volume;
            channel.right_capture_volume = configure.right_capture_volume;
            if (configure.enable_playback) {
                channel.position = 0.0;
                channel.playing = true;
            }
            break;
        }
        case CommandId::ConfigurePsgNoise: {
            auto& configure = command.configure_psg_noise;
            auto& channel = channels[configure.channel];
            channel.encoding = Encoding::Psg;
            channel.is_noise = true;
            channel.left_channel_volume = configure.left_channel_volume;
            channel.right_channel_volume = configure.right_channel_volume;
            channel.left_capture_volume = configure.left_capture_volume;
            channel.right_capture_volume = configure.right_capture_volume;
            if (configure.enable_playback) {
                channel.position = 0.0;
                channel.playing = true;
            }
            break;
        }
        case CommandId::UpdateState: {
            MasterState master{0, 0};
            std::memcpy(shared_memory->GetPointer(master_state_offset), &master, sizeof(master));

            u32 output_index = 0;
            for (u32 i = 0; i < ChannelCount; ++i) {
                if ((acquired_channel_mask & (1 << i)) == 0)
                    continue;
                ChannelState state;
                state.active = channels[i].playing;
                state.adpcm_predictor = channels[i].block1_adpcm_state.predictor;
                state.adpcm_step_index = channels[i].block1_adpcm_state.step_index;
                state.zero = 0;
                std::memcpy(
                    shared_memory->GetPointer(channel_state_offset + sizeof(state) * output_index),
                    &state, sizeof(state));
                ++output_index;
            }

            for (u32 i = 0; i < MaxCaptureUnits; ++i) {
                if (!capture_units[i])
                    continue;
                CaptureState state;
                state.active = false;
                state.zero = 0;
                std::memcpy(shared_memory->GetPointer(capture_state_offset + sizeof(state) * i),
                            &state, sizeof(state));
            }

            break;
        }
        default:
            LOG_ERROR(Service_CSND, "Unimplemented command ID 0x{:X}", command.command_id);
        }
    }

    *shared_memory->GetPointer(addr + offsetof(Type0Command, finished)) = 1;

    rb.Push(ResultSuccess);
}

void CSND_SND::AcquireSoundChannels(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    // This is "almost" hardcoded, as in CSND initializes this with some code during sysmodule
    // startup, but it always compute to the same value.
    acquired_channel_mask = 0xFFFFFF00;

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(acquired_channel_mask);

    LOG_WARNING(Service_CSND, "(STUBBED) called");
}

void CSND_SND::ReleaseSoundChannels(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    acquired_channel_mask = 0;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_WARNING(Service_CSND, "(STUBBED) called");
}

void CSND_SND::AcquireCapUnit(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    if (capture_units[0] && capture_units[1]) {
        LOG_WARNING(Service_CSND, "No more capture units available");
        rb.Push(Result(ErrorDescription::InvalidResultValue, ErrorModule::CSND,
                       ErrorSummary::OutOfResource, ErrorLevel::Status));
        rb.Skip(1, false);
        return;
    }
    rb.Push(ResultSuccess);

    if (capture_units[0]) {
        capture_units[1] = true;
        rb.Push<u32>(1);
    } else {
        capture_units[0] = true;
        rb.Push<u32>(0);
    }

    LOG_WARNING(Service_CSND, "(STUBBED) called");
}

void CSND_SND::ReleaseCapUnit(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 index = rp.Pop<u32>();

    capture_units[index] = false;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_WARNING(Service_CSND, "(STUBBED) called, capture_unit_index={}", index);
}

void CSND_SND::FlushDataCache(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    [[maybe_unused]] const VAddr address = rp.Pop<u32>();
    [[maybe_unused]] const u32 size = rp.Pop<u32>();
    const auto process = rp.PopObject<Kernel::Process>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_TRACE(Service_CSND, "(STUBBED) called address=0x{:08X}, size=0x{:08X}, process={}", address,
              size, process->process_id);
}

void CSND_SND::StoreDataCache(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    [[maybe_unused]] const VAddr address = rp.Pop<u32>();
    [[maybe_unused]] const u32 size = rp.Pop<u32>();
    const auto process = rp.PopObject<Kernel::Process>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_TRACE(Service_CSND, "(STUBBED) called address=0x{:08X}, size=0x{:08X}, process={}", address,
              size, process->process_id);
}

void CSND_SND::InvalidateDataCache(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    [[maybe_unused]] const VAddr address = rp.Pop<u32>();
    [[maybe_unused]] const u32 size = rp.Pop<u32>();
    const auto process = rp.PopObject<Kernel::Process>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_TRACE(Service_CSND, "(STUBBED) called address=0x{:08X}, size=0x{:08X}, process={}", address,
              size, process->process_id);
}

void CSND_SND::Reset(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    for (auto& channel : channels) {
        channel.playing = false;
        channel.position = 0.0;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_WARNING(Service_CSND, "(STUBBED) called");
}

void CSND_SND::DecodeBlock(const Channel& channel, std::vector<s16>& output, PAddr address,
                           u32 size, AdpcmState adpcm_state) {
    output.clear();
    if (address == 0 || size == 0 || channel.encoding == Encoding::Psg) {
        return;
    }
    if (size > MaxDecodedBytes) {
        LOG_WARNING(Service_CSND, "Block size 0x{:X} exceeds decode cap 0x{:X}; truncating", size,
                    MaxDecodedBytes);
        size = MaxDecodedBytes;
    }
    const u8* data = system.Memory().GetPhysicalPointer(address);
    if (!data) {
        LOG_WARNING(Service_CSND, "Unable to read CSND block at physical address 0x{:08X}", address);
        return;
    }
    switch (channel.encoding) {
    case Encoding::Pcm8: {
        output.reserve(size);
        for (u32 i = 0; i < size; ++i) {
            output.push_back(static_cast<s16>(static_cast<u16>(data[i]) << 8));
        }
        break;
    }
    case Encoding::Pcm16: {
        const u32 sample_count = size / 2;
        output.reserve(sample_count);
        for (u32 i = 0; i < sample_count; ++i) {
            s16 sample;
            std::memcpy(&sample, data + i * 2, sizeof(sample));
            output.push_back(sample);
        }
        break;
    }
    case Encoding::Adpcm: {
        // IMA-ADPCM, 4-bit nibbles, as used by CWAV files.
        int predictor = adpcm_state.predictor;
        int step_index = std::clamp(static_cast<int>(adpcm_state.step_index), 0, 88);
        int step = ima_step_table[step_index];
        output.reserve(size * 2);
        for (u32 i = 0; i < size; ++i) {
            for (int nibble : {data[i] >> 4, data[i] & 0xF}) {
                int diff = step >> 3;
                if (nibble & 1) {
                    diff += step >> 2;
                }
                if (nibble & 2) {
                    diff += step >> 1;
                }
                if (nibble & 4) {
                    diff += step;
                }
                predictor += (nibble & 8) ? -diff : diff;
                predictor = std::clamp(predictor, -32768, 32767);
                output.push_back(static_cast<s16>(predictor));
                step_index = std::clamp(step_index + ima_index_table[nibble], 0, 88);
                step = ima_step_table[step_index];
            }
        }
        break;
    }
    case Encoding::Psg:
        break;
    }
}

void CSND_SND::StartChannel(u32 index) {
    Channel& channel = channels[index];
    channel.position = 0.0;
    channel.on_block2 = false;
    DecodeBlock(channel, channel.block1_samples, channel.block1_address, channel.block1_size,
                channel.block1_adpcm_state);
    if (channel.loop_mode == LoopMode::Normal || channel.loop_mode == LoopMode::ConstantSize) {
        DecodeBlock(channel, channel.block2_samples, channel.block2_address, channel.block2_size,
                    channel.block2_adpcm_state);
    }
    channel.playing = true;
}

s16 CSND_SND::GetChannelSample(Channel& channel) {
    if (channel.encoding == Encoding::Psg) {
        if (channel.is_noise) {
            // Simple 16-bit LFSR white noise generator.
            channel.noise_lfsr = static_cast<u16>((channel.noise_lfsr >> 1) ^
                                                  (-(channel.noise_lfsr & 1) & 0xB400u));
            return (channel.noise_lfsr & 1) ? 32767 : -32767;
        }
        // PSG square wave, 32 source samples per full cycle.
        const u32 phase = static_cast<u32>(channel.position) % 32;
        const u32 high_samples = channel.psg_duty == 7 ? 0 : static_cast<u32>(channel.psg_duty) + 1;
        if (high_samples == 0) {
            return 0;
        }
        channel.position += GetChannelStep(channel);
        return phase < high_samples ? 32767 : -32767;
    }

    auto& block = channel.on_block2 ? channel.block2_samples : channel.block1_samples;
    if (block.empty()) {
        channel.playing = false;
        return 0;
    }

    double position = channel.position;
    u32 index = static_cast<u32>(position);
    if (index >= block.size()) {
        // Reached the end of the current block.
        if (!channel.on_block2 && channel.loop_mode != LoopMode::Manual &&
            !channel.block2_samples.empty()) {
            // Normal / ConstantSize looping: play block 1 once, then repeat block 2 forever.
            channel.on_block2 = true;
            channel.position = 0.0;
            block = channel.block2_samples;
        } else if (channel.loop_mode == LoopMode::Manual) {
            // Manual mode: play block 1 endlessly, ignoring the size field.
            channel.position = std::fmod(channel.position, static_cast<double>(block.size()));
        } else {
            // One-shot (or normal looping without loop data): stop playing.
            channel.playing = false;
            return 0;
        }
        // Re-read the position, which may have been reset by the block transition above.
        position = channel.position;
        index = static_cast<u32>(position);
        if (index >= block.size()) {
            channel.playing = false;
            return 0;
        }
    }

    s16 sample;
    const double fraction = position - static_cast<double>(index);
    if (channel.linear_interpolation && index + 1 < block.size()) {
        const s32 sample0 = block[index];
        const s32 sample1 = block[index + 1];
        sample = static_cast<s16>(sample0 + static_cast<s32>((sample1 - sample0) * fraction));
    } else {
        sample = block[index];
    }
    channel.position += GetChannelStep(channel);
    return sample;
}

void CSND_SND::MixChannel(u32 index, AudioCore::StereoFrame16& frame) {
    Channel& channel = channels[index];
    const s32 left_volume = channel.left_channel_volume;
    const s32 right_volume = channel.right_channel_volume;
    for (std::size_t s = 0; s < frame.size(); ++s) {
        const s16 sample = GetChannelSample(channel);
        if (!channel.playing) {
            break; // One-shot ended; the rest of the frame stays silent.
        }
        frame[s][0] = static_cast<s16>(std::clamp((sample * left_volume) >> 15, -32768, 32767));
        frame[s][1] = static_cast<s16>(std::clamp((sample * right_volume) >> 15, -32768, 32767));
    }
}

void CSND_SND::AudioTickCallback(s64 cycles_late) {
    if (shared_memory) {
        AudioCore::StereoFrame16 frame{};
        bool any_playing = false;
        for (u32 i = 0; i < ChannelCount; ++i) {
            if (channels[i].playing) {
                any_playing = true;
                MixChannel(i, frame);
            }
        }
        if (any_playing) {
            system.DSP().OutputFrame(frame);
        }
    }

    const double time_scale =
        Settings::values.enable_realtime_audio ? std::max(0.01, system.GetStableFrameTimeScale())
                                               : 1.0;
    s64 adjusted_ticks = static_cast<s64>(audio_frame_ticks / time_scale - cycles_late);
    system.CoreTiming().ScheduleEvent(adjusted_ticks, tick_event);
}

CSND_SND::CSND_SND(Core::System& system) : ServiceFramework("csnd:SND", 4), system(system) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, &CSND_SND::Initialize, "Initialize"},
        {0x0002, &CSND_SND::Shutdown, "Shutdown"},
        {0x0003, &CSND_SND::ExecuteCommands, "ExecuteCommands"},
        {0x0004, nullptr, "ExecuteType1Commands"},
        {0x0005, &CSND_SND::AcquireSoundChannels, "AcquireSoundChannels"},
        {0x0006, &CSND_SND::ReleaseSoundChannels, "ReleaseSoundChannels"},
        {0x0007, &CSND_SND::AcquireCapUnit, "AcquireCapUnit"},
        {0x0008, &CSND_SND::ReleaseCapUnit, "ReleaseCapUnit"},
        {0x0009, &CSND_SND::FlushDataCache, "FlushDataCache"},
        {0x000A, &CSND_SND::StoreDataCache, "StoreDataCache"},
        {0x000B, &CSND_SND::InvalidateDataCache, "InvalidateDataCache"},
        {0x000C, &CSND_SND::Reset, "Reset"},
        // clang-format on
    };

    RegisterHandlers(functions);

    tick_event = system.CoreTiming().RegisterEvent(
        "CSND_SND::audio_tick", [this](u64, s64 cycles_late) { AudioTickCallback(cycles_late); });
    system.CoreTiming().ScheduleEvent(audio_frame_ticks, tick_event);
};

void InstallInterfaces(Core::System& system) {
    auto& service_manager = system.ServiceManager();
    std::make_shared<CSND_SND>(system)->InstallAsService(service_manager);
}

} // namespace Service::CSND
