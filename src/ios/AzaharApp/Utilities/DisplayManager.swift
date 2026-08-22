// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import UIKit
import SwiftUI
import QuartzCore
import Metal

// MARK: - Display Mode

/// Defines how the two 3DS screens are distributed across the iPhone and external display.
enum ExternalDisplayMode: Int, CaseIterable, Identifiable {
    /// Both 3DS screens on the iPhone (normal mode).
    case iPhoneDualScreen = 0
    /// Top screen on external, bottom on iPhone with touch.
    case externalTopScreen = 1
    /// Both 3DS screens on the external display; iPhone is controls-only.
    case externalBothScreens = 2
    /// Large top + small bottom on the external display; iPhone is controls-only.
    case externalLargeTopSmallBottom = 3
    /// Mirror the full iPhone layout onto the external display.
    case mirror = 4

    var id: Int { rawValue }

    var displayName: String {
        switch self {
        case .iPhoneDualScreen:
            return "iPhone Dual Screen"
        case .externalTopScreen:
            return "External Top Screen"
        case .externalBothScreens:
            return "External Both Screens"
        case .externalLargeTopSmallBottom:
            return "External Large Top + Small Bottom"
        case .mirror:
            return "Mirror"
        }
    }

    var description: String {
        switch self {
        case .iPhoneDualScreen:
            return "Both 3DS screens on iPhone. Normal handheld mode."
        case .externalTopScreen:
            return "Top screen fullscreen on TV. Bottom screen + touch on iPhone."
        case .externalBothScreens:
            return "Both screens on TV. iPhone shows only controls."
        case .externalLargeTopSmallBottom:
            return "Large top screen + small bottom screen on TV. iPhone is controls."
        case .mirror:
            return "Mirror the full emulator layout to the external display."
        }
    }

    var systemImage: String {
        switch self {
        case .iPhoneDualScreen:
            return "iphone"
        case .externalTopScreen:
            return "tv"
        case .externalBothScreens:
            return "tv.badge.wifi"
        case .externalLargeTopSmallBottom:
            return "rectangle.split.2x1"
        case .mirror:
            return "rectangle.on.rectangle"
        }
    }
}

// MARK: - Screen Destination

/// Where a particular 3DS screen is rendered.
enum ScreenDestination {
    case iPhone
    case external
    case hidden
}

// MARK: - Controls Destination

/// Where the touch/button controls are displayed.
enum ControlsDestination {
    case iPhone
    case external
    case both
    case hidden
}

// MARK: - Display Configuration

/// The complete configuration describing a display mode's screen routing.
struct DisplayConfiguration {
    let topScreenDestination: ScreenDestination
    let bottomScreenDestination: ScreenDestination
    let controlsDestination: ControlsDestination
    let iPhoneShowsTopScreen: Bool
    let iPhoneShowsBottomScreen: Bool
    let externalShowsTopScreen: Bool
    let externalShowsBottomScreen: Bool

    /// Predefined configurations for each mode.
    static func forMode(_ mode: ExternalDisplayMode) -> DisplayConfiguration {
        switch mode {
        case .iPhoneDualScreen:
            return DisplayConfiguration(
                topScreenDestination: .iPhone,
                bottomScreenDestination: .iPhone,
                controlsDestination: .iPhone,
                iPhoneShowsTopScreen: true,
                iPhoneShowsBottomScreen: true,
                externalShowsTopScreen: false,
                externalShowsBottomScreen: false
            )
        case .externalTopScreen:
            return DisplayConfiguration(
                topScreenDestination: .external,
                bottomScreenDestination: .iPhone,
                controlsDestination: .iPhone,
                iPhoneShowsTopScreen: false,
                iPhoneShowsBottomScreen: true,
                externalShowsTopScreen: true,
                externalShowsBottomScreen: false
            )
        case .externalBothScreens:
            return DisplayConfiguration(
                topScreenDestination: .external,
                bottomScreenDestination: .external,
                controlsDestination: .iPhone,
                iPhoneShowsTopScreen: false,
                iPhoneShowsBottomScreen: false,
                externalShowsTopScreen: true,
                externalShowsBottomScreen: true
            )
        case .externalLargeTopSmallBottom:
            return DisplayConfiguration(
                topScreenDestination: .external,
                bottomScreenDestination: .external,
                controlsDestination: .iPhone,
                iPhoneShowsTopScreen: false,
                iPhoneShowsBottomScreen: false,
                externalShowsTopScreen: true,
                externalShowsBottomScreen: true
            )
        case .mirror:
            return DisplayConfiguration(
                topScreenDestination: .iPhone,
                bottomScreenDestination: .iPhone,
                controlsDestination: .both,
                iPhoneShowsTopScreen: true,
                iPhoneShowsBottomScreen: true,
                externalShowsTopScreen: true,
                externalShowsBottomScreen: true
            )
        }
    }
}

// MARK: - External Layout Configuration

/// Configurable relative sizing for the external display layout.
/// Percentages are relative layout preferences, not fixed pixel dimensions.
/// The layout engine ensures both screens fit on the display at the correct aspect ratios.
struct ExternalLayoutConfiguration {
    /// Relative height fraction for the top screen (0.0 – 1.0).
    var topHeightFraction: CGFloat
    /// Relative height fraction for the bottom screen (0.0 – 1.0).
    var bottomHeightFraction: CGFloat
    /// Spacing between screens in points.
    var spacing: CGFloat
    /// Margin around all screens in points.
    var margin: CGFloat

    /// Predefined presets.
    static let equalScreens = ExternalLayoutConfiguration(
        topHeightFraction: 0.5, bottomHeightFraction: 0.5, spacing: 12, margin: 20
    )
    static let largeTopSmallBottom = ExternalLayoutConfiguration(
        topHeightFraction: 0.72, bottomHeightFraction: 0.28, spacing: 8, margin: 16
    )
    static let largeTopMediumBottom = ExternalLayoutConfiguration(
        topHeightFraction: 0.6, bottomHeightFraction: 0.4, spacing: 8, margin: 16
    )
    static let topOnly = ExternalLayoutConfiguration(
        topHeightFraction: 1.0, bottomHeightFraction: 0.0, spacing: 0, margin: 0
    )
}

// MARK: - Layout Frame

/// A calculated frame for one screen within a display area.
struct ScreenFrame {
    let origin: CGPoint
    let size: CGSize

    var rect: CGRect { CGRect(origin: origin, size: size) }
    var centerX: CGFloat { origin.x + size.width / 2 }
    var centerY: CGFloat { origin.y + size.height / 2 }
}

// MARK: - Display Manager

/// Manages external display detection, UIWindow lifecycle, and mode switching.
/// Designed as the single source of truth for multi-display state.
class DisplayManager: ObservableObject {
    static let shared = DisplayManager()

    // MARK: Published State

    @Published var isExternalDisplayConnected = false
    @Published var displayMode: ExternalDisplayMode {
        didSet {
            guard displayMode != oldValue else { return }
            UserDefaults.standard.set(displayMode.rawValue, forKey: Self.displayModeKey)
            applyMode()
            notifyModeChanged()
        }
    }

    /// The computed configuration for the current display mode.
    var configuration: DisplayConfiguration {
        DisplayConfiguration.forMode(displayMode)
    }

    /// Layout preset for the external display (only relevant for modes that use it).
    @Published var externalLayout: ExternalLayoutConfiguration {
        didSet {
            saveExternalLayout()
        }
    }

    // MARK: Internal State

    private(set) var externalScreen: UIScreen?
    private var externalWindow: UIWindow?
    private var metalLayer: CAMetalLayer?
    private var displayLink: CADisplayLink?
    private var isSurfaceActive = false

    private var screenConnectObserver: NSObjectProtocol?
    private var screenDisconnectObserver: NSObjectProtocol?

    private static let displayModeKey = "external_display_mode_v2"
    private static let layoutTopKey = "external_layout_top_fraction"
    private static let layoutBottomKey = "external_layout_bottom_fraction"
    private static let layoutSpacingKey = "external_layout_spacing"
    private static let layoutMarginKey = "external_layout_margin"

    // MARK: Init

    private init() {
        // Restore persisted mode.
        let savedRaw = UserDefaults.standard.integer(forKey: Self.displayModeKey)
        self.displayMode = ExternalDisplayMode(rawValue: savedRaw) ?? .iPhoneDualScreen
        self.externalLayout = Self.loadExternalLayout()

        AppLogger.info("[DisplayManager] Initialized with mode: \(displayMode.displayName)")
        setupScreenObservers()
        detectExistingDisplay()
    }

    deinit {
        if let obs = screenConnectObserver {
            NotificationCenter.default.removeObserver(obs)
        }
        if let obs = screenDisconnectObserver {
            NotificationCenter.default.removeObserver(obs)
        }
        stopDisplayLink()
    }

    // MARK: - Public API

    /// Force re-initialization of the external display (for AirPlay/HDMI manual init).
    func forceExternalDisplay() {
        AppLogger.info("[DisplayManager] forceExternalDisplay requested")

        if isExternalDisplayConnected {
            applyMode()
            return
        }

        // Try UIScreen.screens.
        if UIScreen.screens.count > 1 {
            handleScreenConnected(UIScreen.screens[1])
            return
        }

        // Try mirrored screen.
        if let mirrored = UIScreen.main.mirrored {
            AppLogger.info("[DisplayManager] Using mirrored screen as external")
            handleScreenConnected(mirrored)
            return
        }

        // Search connected scenes for an external screen.
        for scene in UIApplication.shared.connectedScenes {
            if let ws = scene as? UIWindowScene, ws.screen != UIScreen.main {
                handleScreenConnected(ws.screen)
                return
            }
        }

        AppLogger.warning("[DisplayManager] No external display found")
    }

    /// Clean up external display resources.
    func teardownExternalDisplay() {
        AppLogger.info("[DisplayManager] Teardown external display")
        stopDisplayLink()
        metalLayer = nil
        externalWindow?.isHidden = true
        externalWindow = nil
        externalScreen = nil
        isExternalDisplayConnected = false

        az_emu_secondary_surface_destroy()
        notifyModeChanged()
    }

    // MARK: - Screen Observers

    private func setupScreenObservers() {
        screenConnectObserver = NotificationCenter.default.addObserver(
            forName: UIScreen.didConnectNotification, object: nil, queue: .main
        ) { [weak self] notification in
            guard let screen = notification.object as? UIScreen else { return }
            self?.handleScreenConnected(screen)
        }

        screenDisconnectObserver = NotificationCenter.default.addObserver(
            forName: UIScreen.didDisconnectNotification, object: nil, queue: .main
        ) { [weak self] _ in
            self?.handleScreenDisconnected()
        }

        // Also listen for scene-based connections (iOS 13+).
        NotificationCenter.default.addObserver(
            forName: Notification.Name("ExternalSceneConnected"), object: nil, queue: .main
        ) { [weak self] notification in
            guard let screen = notification.object as? UIScreen else { return }
            if let window = notification.userInfo?["window"] as? UIWindow {
                self?.externalWindow = window
            }
            self?.handleScreenConnected(screen)
        }

        NotificationCenter.default.addObserver(
            forName: Notification.Name("ExternalSceneDisconnected"), object: nil, queue: .main
        ) { [weak self] _ in
            self?.handleScreenDisconnected()
        }
    }

    private func detectExistingDisplay() {
        if UIScreen.screens.count > 1 {
            handleScreenConnected(UIScreen.screens[1])
        }
    }

    // MARK: - Connection Handling

    private func handleScreenConnected(_ screen: UIScreen) {
        AppLogger.info("[DisplayManager] Screen connected: \(screen.bounds)")

        externalScreen = screen
        isExternalDisplayConnected = true

        // Set best resolution.
        if let best = screen.availableModes.max(by: { $0.size.width * $0.size.height < $1.size.width * $1.size.height }) {
            screen.currentMode = best
            AppLogger.info("[DisplayManager] Set screen mode: \(best.size.width)x\(best.size.height)")
        }
        screen.overscanCompensation = .none

        // Create or update external window.
        setupExternalWindow(on: screen)

        // Apply current mode.
        applyMode()

        notifyModeChanged()
    }

    private func handleScreenDisconnected() {
        AppLogger.info("[DisplayManager] Screen disconnected")
        teardownExternalDisplay()
    }

    // MARK: - External Window

    private func setupExternalWindow(on screen: UIScreen) {
        if let existing = externalWindow {
            existing.screen = screen
            existing.frame = screen.bounds
            existing.isHidden = false
            existing.makeKeyAndVisible()
            // Re-establish Metal surface.
            configureMetalLayer(for: screen)
            return
        }

        let window: UIWindow
        if #available(iOS 13.0, *),
           let ws = UIApplication.shared.connectedScenes
               .compactMap({ $0 as? UIWindowScene })
               .first(where: { $0.screen == screen }) {
            window = UIWindow(windowScene: ws)
        } else {
            window = UIWindow(frame: screen.bounds)
        }

        window.screen = screen
        window.frame = screen.bounds
        window.backgroundColor = .black
        window.windowLevel = .normal

        let vc = UIViewController()
        vc.view.backgroundColor = .black
        vc.view.frame = screen.bounds
        vc.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        window.rootViewController = vc
        window.isHidden = false
        window.makeKeyAndVisible()

        // Bring the main window back to key.
        DispatchQueue.main.async {
            UIApplication.shared.windows.first { $0 != window }?.makeKey()
        }

        externalWindow = window
        configureMetalLayer(for: screen)
    }

    private func configureMetalLayer(for screen: UIScreen) {
        guard let viewController = externalWindow?.rootViewController else { return }

        let view = viewController.view

        // Remove previous metal layer sublayer if any.
        if let old = metalLayer {
            old.removeFromSuperlayer()
        }

        let layer = CAMetalLayer()
        layer.frame = view.bounds
        layer.device = MTLCreateSystemDefaultDevice()
        layer.pixelFormat = .bgra8Unorm
        layer.framebufferOnly = false
        layer.contentsScale = screen.nativeScale
        layer.drawableSize = CGSize(
            width: view.bounds.width * screen.nativeScale,
            height: view.bounds.height * screen.nativeScale
        )
        view.layer.insertSublayer(layer, at: 0)

        metalLayer = layer
        let scale = Float(screen.nativeScale)
        az_emu_secondary_surface_set(
            Unmanaged.passUnretained(layer).toOpaque(),
            scale
        )
        isSurfaceActive = true

        AppLogger.info("[DisplayManager] Metal layer configured: \(layer.drawableSize)")
    }

    // MARK: - Mode Application

    /// Maps each ExternalDisplayMode to the C++ renderer layout settings and triggers a re-layout.
    private func applyMode() {
        AppLogger.info("[DisplayManager] Applying mode: \(displayMode.displayName)")

        switch displayMode {
        case .iPhoneDualScreen:
            // Normal dual-screen on iPhone; no external rendering.
            az_setting_set_int("Layout", "layout_option", 2) // LargeScreen
            az_setting_set_bool("Layout", "swap_screen", false)

        case .externalTopScreen:
            // Top screen → external (secondary), bottom screen → iPhone (primary).
            az_setting_set_int("Layout", "layout_option", 4) // SeparateWindows
            az_setting_set_int("Layout", "secondary_display_layout", 4) // OppositeScreenOnly
            az_setting_set_bool("Layout", "swap_screen", true)
            az_setting_set_int("Layout", "upright_screen", 1) // Top on secondary

        case .externalBothScreens:
            // Both screens on external; iPhone shows controls.
            az_setting_set_int("Layout", "layout_option", 4) // SeparateWindows
            az_setting_set_int("Layout", "secondary_display_layout", 5) // Original
            az_setting_set_bool("Layout", "swap_screen", false)

        case .externalLargeTopSmallBottom:
            // Both screens on external with large top emphasis.
            az_setting_set_int("Layout", "layout_option", 4) // SeparateWindows
            az_setting_set_int("Layout", "secondary_display_layout", 5) // Original
            az_setting_set_bool("Layout", "swap_screen", false)
            // The large_top_small_bottom layout is handled in the external MetalView
            // by rendering with the ExternalLayoutConfiguration的比例.

        case .mirror:
            // Mirror the primary layout to external.
            az_setting_set_int("Layout", "layout_option", 2) // LargeScreen
            az_setting_set_int("Layout", "secondary_display_layout", 2) // LargeScreen
            az_setting_set_bool("Layout", "swap_screen", false)
        }

        az_reload_settings()

        let portrait = UIScreen.main.bounds.height > UIScreen.main.bounds.width
        az_update_framebuffer(portrait)

        // Re-establish surface after layout change.
        if let screen = externalScreen {
            configureMetalLayer(for: screen)
        }
    }

    // MARK: - Display Link

    func startDisplayLink() {
        guard displayLink == nil else { return }
        let link = CADisplayLink(target: self, selector: #selector(drawExternalFrame))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 120, preferred: 60)
        link.add(to: .main, forMode: .common)
        displayLink = link
        AppLogger.info("[DisplayManager] DisplayLink started")
    }

    func stopDisplayLink() {
        displayLink?.invalidate()
        displayLink = nil
    }

    @objc private func drawExternalFrame() {
        if az_is_running() && !az_is_paused() {
            // The C++ renderer presents to the secondary surface automatically.
        }
    }

    // MARK: - Persistence

    private func saveExternalLayout() {
        UserDefaults.standard.set(externalLayout.topHeightFraction, forKey: Self.layoutTopKey)
        UserDefaults.standard.set(externalLayout.bottomHeightFraction, forKey: Self.layoutBottomKey)
        UserDefaults.standard.set(externalLayout.spacing, forKey: Self.layoutSpacingKey)
        UserDefaults.standard.set(externalLayout.margin, forKey: Self.layoutMarginKey)
    }

    private static func loadExternalLayout() -> ExternalLayoutConfiguration {
        let defaults = UserDefaults.standard
        let top = defaults.object(forKey: layoutTopKey) != nil
            ? CGFloat(defaults.double(forKey: layoutTopKey))
            : ExternalLayoutConfiguration.largeTopSmallBottom.topHeightFraction
        let bottom = defaults.object(forKey: layoutBottomKey) != nil
            ? CGFloat(defaults.double(forKey: layoutBottomKey))
            : ExternalLayoutConfiguration.largeTopSmallBottom.bottomHeightFraction
        let spacing = defaults.object(forKey: layoutSpacingKey) != nil
            ? CGFloat(defaults.double(forKey: layoutSpacingKey))
            : ExternalLayoutConfiguration.largeTopSmallBottom.spacing
        let margin = defaults.object(forKey: layoutMarginKey) != nil
            ? CGFloat(defaults.double(forKey: layoutMarginKey))
            : ExternalLayoutConfiguration.largeTopSmallBottom.margin
        return ExternalLayoutConfiguration(
            topHeightFraction: top, bottomHeightFraction: bottom,
            spacing: spacing, margin: margin
        )
    }

    // MARK: - Notification

    private func notifyModeChanged() {
        NotificationCenter.default.post(
            name: .externalDisplayModeChanged, object: nil,
            userInfo: [
                "mode": displayMode.rawValue,
                "connected": isExternalDisplayConnected
            ]
        )
    }
}

// MARK: - Notification Name

extension Notification.Name {
    static let externalDisplayModeChanged = Notification.Name("ExternalDisplayModeChanged")
}

// MARK: - Layout Calculator

/// Computes aspect-fit screen frames for the external display based on the
/// display's actual dimensions and the chosen layout configuration.
enum LayoutCalculator {
    /// 3DS top screen aspect ratio: 400:240 = 5:3.
    static let topAspectRatio: CGFloat = 5.0 / 3.0
    /// 3DS bottom screen aspect ratio: 320:240 = 4:3.
    static let bottomAspectRatio: CGFloat = 4.0 / 3.0

    /// Calculates the frames for both 3DS screens within a given display size.
    /// - Parameters:
    ///   - displaySize: The usable area of the external display (after safe area insets).
    ///   - layout: The relative sizing configuration.
    /// - Returns: A tuple of (topFrame, bottomFrame). The unused frame has zero size.
    static func calculateFrames(
        for displaySize: CGSize,
        layout: ExternalLayoutConfiguration
    ) -> (top: ScreenFrame, bottom: ScreenFrame) {
        let availableWidth = displaySize.width - 2 * layout.margin
        let totalHeight = displaySize.height - 2 * layout.margin - layout.spacing

        guard availableWidth > 0, totalHeight > 0 else {
            let zero = ScreenFrame(origin: .zero, size: .zero)
            return (zero, zero)
        }

        let topAvailableHeight = totalHeight * layout.topHeightFraction
        let bottomAvailableHeight = totalHeight * layout.bottomHeightFraction

        // Aspect-fit the top screen into its available area.
        let topSize = aspectFit(
            aspectRatio: topAspectRatio,
            into: CGSize(width: availableWidth, height: topAvailableHeight)
        )
        // Aspect-fit the bottom screen into its available area.
        let bottomSize = aspectFit(
            aspectRatio: bottomAspectRatio,
            into: CGSize(width: availableWidth, height: bottomAvailableHeight)
        )

        // Center each screen horizontally and stack vertically.
        let topOrigin = CGPoint(
            x: layout.margin + (availableWidth - topSize.width) / 2,
            y: layout.margin
        )
        let bottomOrigin = CGPoint(
            x: layout.margin + (availableWidth - bottomSize.width) / 2,
            y: layout.margin + topAvailableHeight + layout.spacing
        )

        return (
            top: ScreenFrame(origin: topOrigin, size: topSize),
            bottom: ScreenFrame(origin: bottomOrigin, size: bottomSize)
        )
    }

    /// Calculates frames for a "large top + small bottom" layout where
    /// the top screen takes most of the display and the bottom screen
    /// is in the lower-left corner.
    static func calculateLargeTopSmallBottom(
        for displaySize: CGSize,
        layout: ExternalLayoutConfiguration
    ) -> (top: ScreenFrame, bottom: ScreenFrame) {
        let availableWidth = displaySize.width - 2 * layout.margin
        let totalHeight = displaySize.height - 2 * layout.margin - layout.spacing

        guard availableWidth > 0, totalHeight > 0 else {
            let zero = ScreenFrame(origin: .zero, size: .zero)
            return (zero, zero)
        }

        let topAvailableHeight = totalHeight * layout.topHeightFraction
        let bottomAvailableHeight = totalHeight * layout.bottomHeightFraction

        // Top screen: aspect-fit, centered horizontally at the top.
        let topSize = aspectFit(
            aspectRatio: topAspectRatio,
            into: CGSize(width: availableWidth, height: topAvailableHeight)
        )
        let topOrigin = CGPoint(
            x: layout.margin + (availableWidth - topSize.width) / 2,
            y: layout.margin
        )

        // Bottom screen: aspect-fit, positioned in the lower-left.
        let bottomSize = aspectFit(
            aspectRatio: bottomAspectRatio,
            into: CGSize(width: availableWidth * 0.45, height: bottomAvailableHeight)
        )
        let bottomOrigin = CGPoint(
            x: layout.margin,
            y: layout.margin + topAvailableHeight + layout.spacing
        )

        return (
            top: ScreenFrame(origin: topOrigin, size: topSize),
            bottom: ScreenFrame(origin: bottomOrigin, size: bottomSize)
        )
    }

    /// Given a target aspect ratio and a bounding box, returns the largest size
    /// that fits inside the box while preserving the aspect ratio (aspect-fit).
    static func aspectFit(aspectRatio: CGFloat, into boundingBox: CGSize) -> CGSize {
        guard boundingBox.width > 0, boundingBox.height > 0 else { return .zero }

        let widthByHeight = boundingBox.width / boundingBox.height
        if widthByHeight > aspectRatio {
            // Box is wider than needed; match height.
            let width = boundingBox.height * aspectRatio
            return CGSize(width: floor(width), height: floor(boundingBox.height))
        } else {
            // Box is taller than needed; match width.
            let height = boundingBox.width / aspectRatio
            return CGSize(width: floor(boundingBox.width), height: floor(height))
        }
    }
}
