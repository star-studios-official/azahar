// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <utility>
#include <vector>
#include <AL/al.h>
#include <AL/alc.h>
#include "audio_core/input.h"
#include "audio_core/openal_input.h"
#include "audio_core/sink.h"
#include "common/logging/log.h"

namespace AudioCore {

struct OpenALInput::Impl {
    ALCdevice* device = nullptr;
    u8 sample_size_in_bytes = 0;
    u32 actual_sample_rate = 0;
    /// Phase accumulator (in source-sample units) used to resample from the actual device rate to
    /// the rate requested by the application when the two differ.
    double sample_position = 0.0;
};

OpenALInput::OpenALInput(std::string device_id)
    : impl(std::make_unique<Impl>()), device_id(std::move(device_id)) {}

OpenALInput::~OpenALInput() {
    StopSampling();
}

void OpenALInput::StartSampling(const InputParameters& params) {
    if (IsSampling()) {
        return;
    }

    // OpenAL supports unsigned 8-bit and signed 16-bit PCM.
    // TODO: Re-sample the stream.
    if ((params.sample_size == 8 && params.sign == Signedness::Signed) ||
        (params.sample_size == 16 && params.sign == Signedness::Unsigned)) {
        LOG_WARNING(Audio, "Application requested unsupported unsigned PCM format. Falling back to "
                           "supported format.");
    }

    parameters = params;
    impl->sample_size_in_bytes = params.sample_size / 8;

    auto format = params.sample_size == 16 ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8;
    const char* device_name =
        device_id != auto_device_name && !device_id.empty() ? device_id.c_str() : nullptr;

    // The 3DS uses non-standard microphone sample rates (8182/10909/16364/32728 Hz) which are not
    // supported by the capture devices of every platform (e.g. iOS only accepts standard rates such
    // as 48000 Hz). Try the requested rate first, then fall back to a supported one and resample
    // the stream in software.
    constexpr std::array<u32, 4> fallback_rates = {48000, 44100, 16000};
    impl->actual_sample_rate = params.sample_rate;
    impl->sample_position = 0.0;

    ALCdevice* opened_device =
        alcCaptureOpenDevice(device_name, params.sample_rate, format,
                             static_cast<ALsizei>(params.buffer_size));
    if (opened_device == nullptr || alcGetError(opened_device) != ALC_NO_ERROR) {
        for (u32 rate : fallback_rates) {
            if (opened_device != nullptr) {
                alcCaptureCloseDevice(opened_device);
                opened_device = nullptr;
            }
            opened_device = alcCaptureOpenDevice(device_name, rate, format,
                                                 static_cast<ALsizei>(params.buffer_size));
            if (opened_device != nullptr && alcGetError(opened_device) == ALC_NO_ERROR) {
                impl->actual_sample_rate = rate;
                LOG_WARNING(Audio,
                            "Capture device does not support {} Hz, falling back to {} Hz and "
                            "resampling in software",
                            params.sample_rate, rate);
                break;
            }
        }
    }

    if (opened_device == nullptr || alcGetError(opened_device) != ALC_NO_ERROR) {
        LOG_CRITICAL(Audio, "alcCaptureOpenDevice failed");
        if (opened_device != nullptr) {
            alcCaptureCloseDevice(opened_device);
        }
        StopSampling();
        return;
    }
    impl->device = opened_device;

    alcCaptureStart(impl->device);
    auto capture_error = alcGetError(impl->device);
    if (capture_error != ALC_NO_ERROR) {
        LOG_CRITICAL(Audio, "alcCaptureStart failed: {}", capture_error);
        StopSampling();
        return;
    }
}

void OpenALInput::StopSampling() {
    if (impl->device) {
        alcCaptureStop(impl->device);
        alcCaptureCloseDevice(impl->device);
        impl->device = nullptr;
    }
}

bool OpenALInput::IsSampling() {
    return impl->device != nullptr;
}

void OpenALInput::AdjustSampleRate(u32 sample_rate) {
    if (!IsSampling()) {
        return;
    }

    auto new_params = parameters;
    new_params.sample_rate = sample_rate;
    StopSampling();
    StartSampling(new_params);
}

Samples OpenALInput::Read() {
    if (!IsSampling()) {
        return {};
    }

    ALCint samples_captured = 0;
    alcGetIntegerv(impl->device, ALC_CAPTURE_SAMPLES, 1, &samples_captured);
    auto error = alcGetError(impl->device);
    if (error != ALC_NO_ERROR) {
        LOG_WARNING(Audio, "alcGetIntegerv(ALC_CAPTURE_SAMPLES) failed: {}", error);
        return {};
    }

    auto num_samples = std::min(samples_captured, static_cast<ALsizei>(parameters.buffer_size /
                                                                       impl->sample_size_in_bytes));
    Samples samples(num_samples * impl->sample_size_in_bytes);

    alcCaptureSamples(impl->device, samples.data(), num_samples);
    error = alcGetError(impl->device);
    if (error != ALC_NO_ERROR) {
        LOG_WARNING(Audio, "alcCaptureSamples failed: {}", error);
        return {};
    }

    // Resample from the actual device rate to the rate requested by the application when they
    // differ (see StartSampling). Nearest-neighbor is adequate for microphone capture.
    if (impl->actual_sample_rate != parameters.sample_rate && num_samples > 0) {
        const double ratio = static_cast<double>(impl->actual_sample_rate) / parameters.sample_rate;
        const u32 output_samples = static_cast<u32>(num_samples / ratio);

        Samples resampled(output_samples * impl->sample_size_in_bytes);
        if (impl->sample_size_in_bytes == 2) {
            const auto* input = reinterpret_cast<const s16*>(samples.data());
            auto* output = reinterpret_cast<s16*>(resampled.data());
            for (u32 i = 0; i < output_samples; ++i) {
                output[i] = input[std::min(static_cast<u32>(impl->sample_position),
                                           static_cast<u32>(num_samples - 1))];
                impl->sample_position += ratio;
            }
        } else {
            for (u32 i = 0; i < output_samples; ++i) {
                resampled[i] = samples[std::min(static_cast<u32>(impl->sample_position),
                                                static_cast<u32>(num_samples - 1))];
                impl->sample_position += ratio;
            }
        }

        // Keep the phase within the current chunk so it stays accurate across Read() calls.
        impl->sample_position -= num_samples;
        return resampled;
    }

    return samples;
}

std::vector<std::string> ListOpenALInputDevices() {
    const char* devices_str;
    if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT") != AL_FALSE) {
        devices_str = alcGetString(nullptr, ALC_CAPTURE_DEVICE_SPECIFIER);
    } else {
        LOG_WARNING(
            Audio,
            "Missing OpenAL device enumeration extensions, cannot list audio capture devices.");
        return {};
    }

    if (!devices_str || *devices_str == '\0') {
        return {};
    }

    std::vector<std::string> device_list;
    while (*devices_str != '\0') {
        device_list.emplace_back(devices_str);
        devices_str += strlen(devices_str) + 1;
    }
    return device_list;
}

} // namespace AudioCore
