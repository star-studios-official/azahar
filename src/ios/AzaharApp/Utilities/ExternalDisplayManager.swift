// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import UIKit
import SwiftUI
import QuartzCore

/// Manages external displays (AirPlay, HDMI, USB-C) for dual-screen 3DS emulation
class ExternalDisplayManager: ObservableObject {
    static let shared = ExternalDisplayManager()
    
    @Published var isExternalDisplayConnected = false
    @Published var externalScreen: UIScreen?
    @Published var externalWindow: UIWindow?
    @Published var displayMode: ExternalDisplayMode = .topScreenExternal
    
    private var screenNotificationObserver: NSObjectProtocol?
    
    enum ExternalDisplayMode: Int, CaseIterable {
        case topScreenExternal = 0      // Top screen on external, bottom on iPhone
        case bottomScreenExternal = 1   // Bottom screen on external, top on iPhone
        case mirrorBothScreens = 2      // Both screens mirrored on external and iPhone
        case externalFullscreen = 3     // Full dual-screen on external only
        
        var displayName: String {
            switch self {
            case .topScreenExternal:
                return "Top Screen on TV/Monitor"
            case .bottomScreenExternal:
                return "Bottom Screen on TV/Monitor"
            case .mirrorBothScreens:
                return "Mirror Both Screens"
            case .externalFullscreen:
                return "Full Display on TV/Monitor"
            }
        }
        
        var description: String {
            switch self {
            case .topScreenExternal:
                return "3DS top screen on external display, bottom screen on iPhone with touch controls"
            case .bottomScreenExternal:
                return "3DS bottom screen on external display, top screen on iPhone"
            case .mirrorBothScreens:
                return "Both screens shown on external and iPhone"
            case .externalFullscreen:
                return "Both screens on external display only, iPhone shows controls"
            }
        }
    }
    
    private init() {
        setupScreenNotifications()
        checkForExternalDisplay()
    }
    
    private func setupScreenNotifications() {
        // Monitor for external display connection/disconnection
        NotificationCenter.default.addObserver(
            forName: UIScreen.didConnectNotification,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            guard let screen = notification.object as? UIScreen else { return }
            self?.handleExternalDisplayConnected(screen)
        }
        
        NotificationCenter.default.addObserver(
            forName: UIScreen.didDisconnectNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.handleExternalDisplayDisconnected()
        }
    }
    
    private func checkForExternalDisplay() {
        // Check if there's already an external screen connected
        if UIScreen.screens.count > 1 {
            handleExternalDisplayConnected(UIScreen.screens[1])
        }
    }
    
    func handleExternalDisplayConnected(_ screen: UIScreen) {
        print("External display connected: \(screen.bounds)")
        
        externalScreen = screen
        isExternalDisplayConnected = true
        
        // Set preferred display mode for best quality
        if let mode = screen.availableModes.max(by: { $0.size.width < $1.size.width }) {
            screen.currentMode = mode
        }
        
        // ManicEMU-style: Auto-detect and default to fullscreen mode
        let savedMode = UserDefaults.standard.integer(forKey: "external_display_mode")
        if savedMode == 0 && !UserDefaults.standard.bool(forKey: "has_set_external_display_mode") {
            // First-time external display connection - auto-set to fullscreen
            displayMode = .externalFullscreen
            UserDefaults.standard.set(ExternalDisplayMode.externalFullscreen.rawValue, forKey: "external_display_mode")
            UserDefaults.standard.set(true, forKey: "has_set_external_display_mode")
            print("Auto-configured external display for fullscreen (ManicEMU-style)")
        } else {
            displayMode = ExternalDisplayMode(rawValue: savedMode) ?? .externalFullscreen
        }
        
        // Create the external window if the scene delegate hasn't done it already.
        // Idempotent: if a window already exists it just applies the display mode.
        if externalWindow == nil {
            setupExternalWindow(on: screen)
        } else {
            applyDisplayMode()
        }
        
        // Notify orientation-locking controllers to re-evaluate
        NotificationCenter.default.post(name: Notification.Name("ExternalDisplayModeChanged"), object: nil)
    }
    
    func handleExternalDisplayDisconnected() {
        print("External display disconnected")
        
        // Clean up external window
        externalWindow?.isHidden = true
        externalWindow = nil
        externalScreen = nil
        isExternalDisplayConnected = false
        
        // Destroy secondary surface
        az_emu_secondary_surface_destroy()
        
        // Notify orientation-locking controllers to re-evaluate
        NotificationCenter.default.post(name: Notification.Name("ExternalDisplayModeChanged"), object: nil)
    }
    
    func setDisplayMode(_ mode: ExternalDisplayMode) {
        guard mode != displayMode else { return }
        displayMode = mode
        UserDefaults.standard.set(mode.rawValue, forKey: "external_display_mode")
        
        if isExternalDisplayConnected {
            applyDisplayMode()
        }
        
        // Notify orientation-locking controllers to re-evaluate
        NotificationCenter.default.post(name: Notification.Name("ExternalDisplayModeChanged"), object: nil)
    }
    
    func applyDisplayMode() {
        guard let screen = externalScreen else { return }
        
        switch displayMode {
        case .topScreenExternal:
            // Top screen on external display (secondary window), bottom on iPhone (primary).
            // SeparateWindows layout + swap_screen=true means:
            //   primary (iPhone) renders bottom screen, secondary (external) renders top screen.
            az_setting_set_int("Layout", "layout_option", 4)      // SeparateWindows
            az_setting_set_int("Layout", "secondary_display_layout", 0)
            az_setting_set_bool("Layout", "swap_screen", true)
        case .bottomScreenExternal:
            // Bottom screen on external display (secondary window), top on iPhone (primary).
            // SeparateWindows layout + swap_screen=false means:
            //   primary (iPhone) renders top screen, secondary (external) renders bottom screen.
            az_setting_set_int("Layout", "layout_option", 4)      // SeparateWindows
            az_setting_set_int("Layout", "secondary_display_layout", 1)
            az_setting_set_bool("Layout", "swap_screen", false)
        case .mirrorBothScreens:
            // Both screens shown on both displays (same layout everywhere).
            az_setting_set_int("Layout", "layout_option", 2)      // LargeScreen (both screens)
            az_setting_set_int("Layout", "secondary_display_layout", 2)
        case .externalFullscreen:
            // Both screens on external display. Keep the standard layout so the
            // secondary window renders both screens fullscreen; the iPhone shows controls.
            az_setting_set_int("Layout", "layout_option", 2)      // LargeScreen (both screens)
            az_setting_set_int("Layout", "secondary_display_layout", 2)
        }
        
        // Apply the layout changes to the running emulator
        az_reload_settings()
        
        // Create or update external window if needed
        setupExternalWindow(on: screen)
        
        // Recompute framebuffer layouts for both windows
        let portrait = UIScreen.main.bounds.height > UIScreen.main.bounds.width
        az_update_framebuffer(portrait)
    }
    
    private func setupExternalWindow(on screen: UIScreen) {
        // Reuse the existing external window if the scene delegate already made one.
        if let existing = externalWindow {
            if existing.rootViewController == nil {
                existing.rootViewController = UIHostingController(
                    rootView: ExternalDisplayView(displayManager: self)
                )
            }
            existing.isHidden = false
            print("External window reused: \(screen.bounds)")
            return
        }
        
        // Modern approach for iOS 13+: find the window scene created for the
        // external screen (requires UIApplicationSupportsMultipleScenes = true).
        guard let windowScene = UIApplication.shared.connectedScenes
            .compactMap({ $0 as? UIWindowScene })
            .first(where: { $0.screen == screen }) else {
            print("No window scene available for external screen yet")
            return
        }
        
        // Create a hosting controller with MetalView for external display
        let hostingController = UIHostingController(
            rootView: ExternalDisplayView(displayManager: self)
        )
        
        let window = UIWindow(windowScene: windowScene)
        window.rootViewController = hostingController
        window.isHidden = false
        
        externalWindow = window
        
        print("External window setup complete: \(screen.bounds)")
    }
    
    deinit {
        NotificationCenter.default.removeObserver(self)
    }
}

/// View shown on the external display (AirPlay/HDMI)
struct ExternalDisplayView: View {
    @ObservedObject var displayManager: ExternalDisplayManager
    
    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            
            ExternalMetalView()
                .ignoresSafeArea()
            
            // Optional: Show display mode indicator briefly
            VStack {
                Spacer()
                Text(displayManager.displayMode.displayName)
                    .font(.caption)
                    .foregroundStyle(.white.opacity(0.5))
                    .padding(8)
                    .background(.black.opacity(0.3))
                    .clipShape(Capsule())
                    .padding(.bottom, 20)
            }
        }
    }
}

/// Metal view for external display rendering
struct ExternalMetalView: UIViewRepresentable {
    func makeUIView(context: Context) -> ExternalMetalUIView {
        ExternalMetalUIView()
    }
    
    func updateUIView(_ uiView: ExternalMetalUIView, context: Context) {
        // Update if needed
    }
}

/// UIKit view for external display's Metal layer
final class ExternalMetalUIView: UIView {
    private var displayLink: CADisplayLink?
    private var isSurfaceSet = false
    
    override init(frame: CGRect) {
        super.init(frame: frame)
        setupLayer()
        startPresenting()
    }
    
    required init?(coder: NSCoder) {
        fatalError()
    }
    
    override class var layerClass: AnyClass {
        CAMetalLayer.self
    }
    
    private var metalLayer: CAMetalLayer {
        layer as! CAMetalLayer
    }
    
    private func setupLayer() {
        metalLayer.device = MTLCreateSystemDefaultDevice()
        metalLayer.pixelFormat = .bgra8Unorm
        metalLayer.framebufferOnly = true
        
        // Use external screen's scale
        let scale = window?.screen.scale ?? UIScreen.main.scale
        metalLayer.contentsScale = scale
        metalLayer.drawableSize = CGSize(width: bounds.size.width * scale, height: bounds.size.height * scale)
    }
    
    override func layoutSubviews() {
        super.layoutSubviews()
        metalLayer.frame = bounds
        
        let scale = window?.screen.scale ?? UIScreen.main.scale
        metalLayer.contentsScale = scale
        metalLayer.drawableSize = CGSize(width: bounds.size.width * scale, height: bounds.size.height * scale)
        
        if isSurfaceSet {
            az_emu_secondary_surface_set(
                Unmanaged.passUnretained(metalLayer).toOpaque(),
                Float(scale)
            )
        }
    }
    
    func startPresenting() {
        guard !isSurfaceSet else { return }
        
        let scale = Float(metalLayer.contentsScale)
        az_emu_secondary_surface_set(
            Unmanaged.passUnretained(metalLayer).toOpaque(),
            scale
        )
        isSurfaceSet = true
        
        print("External Metal surface set with scale: \(scale)")
    }
    
    func stopPresenting() {
        isSurfaceSet = false
        az_emu_secondary_surface_destroy()
    }
    
    deinit {
        stopPresenting()
    }
}
