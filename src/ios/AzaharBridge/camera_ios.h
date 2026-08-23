// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>

#include "common/common_types.h"
#include "core/frontend/camera/factory.h"
#include "core/frontend/camera/interface.h"
#include "core/hle/service/cam/cam.h"

namespace Camera::IOS {

struct FrameBuffer;

// Placeholders to mean 'use any front/back camera' (mirrors Camera::NDK::*).
inline constexpr std::string_view FrontCameraPlaceholder = "ios:front";
inline constexpr std::string_view BackCameraPlaceholder = "ios:back";

/// AVFoundation-backed camera implementation. Frames are delivered in the
/// format requested by SetFormat (YUV422 packed YUYV or RGB565) at the
/// resolution requested by SetResolution.
class Interface final : public CameraInterface {
public:
    explicit Interface(const std::string& config, const Service::CAM::Flip& flip);
    ~Interface() override;

    void StartCapture() override;
    void StopCapture() override;
    void SetResolution(const Service::CAM::Resolution& resolution) override;
    void SetFlip(Service::CAM::Flip flip) override;
    void SetEffect(Service::CAM::Effect effect) override {}
    void SetFormat(Service::CAM::OutputFormat format) override;
    void SetFrameRate(Service::CAM::FrameRate frame_rate) override {}
    std::vector<u16> ReceiveFrame() override;
    bool IsPreviewAvailable() override;

private:
    std::string config;
    bool is_front = false;

    Service::CAM::Resolution resolution{};
    bool base_mirror{};
    bool base_invert{};
    bool mirror{};
    bool invert{};
    Service::CAM::OutputFormat format{};

    std::shared_ptr<FrameBuffer> buffer;

    // Retained ObjC objects (kept alive while capturing): AVCaptureSession* and
    // the sample-buffer delegate. Managed with __bridge_retained / __bridge_transfer.
    void* session_ptr = nullptr;
    void* delegate_ptr = nullptr;
};

/// Factory that instantiates AVFoundation cameras. Register it with
/// Camera::RegisterFactory("ios", ...).
class Factory final : public CameraFactory {
public:
    std::unique_ptr<CameraInterface> Create(const std::string& config,
                                            const Service::CAM::Flip& flip) override;
};

} // namespace Camera::IOS
