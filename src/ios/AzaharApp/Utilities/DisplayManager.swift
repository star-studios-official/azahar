// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import UIKit
import SwiftUI

// MARK: - Display Mode

/// Stub enum kept for source compatibility. External display modes are
/// disabled on iOS to avoid Vulkan renderer assertion failures when
/// SeparateWindows layout is activated without a secondary window.
enum ExternalDisplayMode: Int, CaseIterable, Identifiable {
    case iPhoneDualScreen = 0

    var id: Int { rawValue }

    var displayName: String { "iPhone Dual Screen" }
    var description: String { "Both 3DS screens on iPhone. Normal handheld mode." }
    var systemImage: String { "iphone" }
}

// MARK: - Screen Destination

enum ScreenDestination {
    case iPhone
    case external
    case hidden
}

// MARK: - Controls Destination

enum ControlsDestination {
    case iPhone
    case external
    case both
    case hidden
}

// MARK: - Display Configuration

/// Describes where each 3DS screen and controls are shown.
struct DisplayConfiguration {
    let topScreenDestination: ScreenDestination
    let bottomScreenDestination: ScreenDestination
    let controlsDestination: ControlsDestination
    let iPhoneShowsTopScreen: Bool
    let iPhoneShowsBottomScreen: Bool
    let externalShowsTopScreen: Bool
    let externalShowsBottomScreen: Bool

    /// Always returns the iPhone-only dual-screen config.
    static func forMode(_ mode: ExternalDisplayMode) -> DisplayConfiguration {
        DisplayConfiguration(
            topScreenDestination: .iPhone,
            bottomScreenDestination: .iPhone,
            controlsDestination: .iPhone,
            iPhoneShowsTopScreen: true,
            iPhoneShowsBottomScreen: true,
            externalShowsTopScreen: false,
            externalShowsBottomScreen: false
        )
    }
}

// MARK: - Display Manager

/// Minimal stub. External display support is disabled to prevent the Vulkan
/// renderer's `ASSERT(secondary_window)` from firing when layout_option is
/// set to SeparateWindows without an actual secondary UIWindow.
class DisplayManager: ObservableObject {
    static let shared = DisplayManager()

    @Published var isExternalDisplayConnected = false
    @Published var displayMode: ExternalDisplayMode = .iPhoneDualScreen

    var configuration: DisplayConfiguration {
        DisplayConfiguration.forMode(displayMode)
    }

    private init() {}

    /// Force layout_option to a safe value. The old external display code
    /// may have persisted SeparateWindows (4) which crashes the Vulkan
    /// renderer via ASSERT(secondary_window). Must be called AFTER config
    /// is loaded (az_reload_settings has already run) so the override sticks.
    static func resetLayoutIfNeeded() {
        let current = az_setting_get_int("Layout", "layout_option", 2)
        if current == 4 { // SeparateWindows — dangerous without a secondary window
            az_setting_set_int("Layout", "layout_option", 2) // LargeScreen
            az_setting_set_bool("Layout", "swap_screen", false)
            az_setting_set_int("Layout", "secondary_display_layout", 0) // None
            az_setting_set_int("Layout", "upright_screen", 0)
            AppLogger.info("[DisplayManager] Reset unsafe layout_option from SeparateWindows to LargeScreen")
        }
    }
}
