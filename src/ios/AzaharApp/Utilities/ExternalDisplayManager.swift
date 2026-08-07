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
                return "Like a real 3DS: Top screen fullscreen on TV, bottom screen on iPhone with touch (recommended)"
            case .bottomScreenExternal:
                return "Bottom screen fullscreen on TV, top screen on iPhone (no touch on TV)"
            case .mirrorBothScreens:
                return "Both screens shown on external and iPhone (mirrored)"
            case .externalFullscreen:
                return "Both screens side-by-side on TV, iPhone shows only controls"
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
            //   primary (iPhone) renders bottom screen, secondary (external) renders top screen fullscreen.
            az_setting_set_int("Layout", "layout_option", 4)      // SeparateWindows
            az_setting_set_int("Layout", "secondary_display_layout", 5) // SingleScreen (fullscreen)
            az_setting_set_bool("Layout", "swap_screen", true)
            az_setting_set_int("Layout", "upright_screen", 1)      // Top screen on secondary
        case .bottomScreenExternal:
            // Bottom screen on external display (secondary window), top on iPhone (primary).
            // SeparateWindows layout + swap_screen=false means:
            //   primary (iPhone) renders top screen, secondary (external) renders bottom screen fullscreen.
            az_setting_set_int("Layout", "layout_option", 4)      // SeparateWindows
            az_setting_set_int("Layout", "secondary_display_layout", 5) // SingleScreen (fullscreen)
            az_setting_set_bool("Layout", "swap_screen", false)
            az_setting_set_int("Layout", "upright_screen", 0)      // Bottom screen on secondary
        case .mirrorBothScreens:
            // Both screens shown on both displays (same layout everywhere).
            // Use LargeScreen layout which shows both screens side-by-side or stacked.
            az_setting_set_int("Layout", "layout_option", 2)      // LargeScreen (both screens)
            az_setting_set_int("Layout", "secondary_display_layout", 2) // LargeScreen on secondary too
        case .externalFullscreen:
            // Both screens on external display in optimal layout for TV.
            // The secondary window renders both screens fullscreen; iPhone shows controls only.
            az_setting_set_int("Layout", "layout_option", 2)      // LargeScreen (both screens)
            az_setting_set_int("Layout", "secondary_display_layout", 2) // LargeScreen fullscreen on external
        }
        
        // Apply the layout changes to the running emulator
        az_reload_settings()
        
        // Create or update external window if needed
        setupExternalWindow(on: screen)
        
        // Recompute framebuffer layouts for both windows
        let portrait = UIScreen.main.bounds.height > UIScreen.main.bounds.width
        az_update_framebuffer(portrait)
        
        print("[ExternalDisplay] Applied display mode: \(displayMode.displayName)")
    }
    
    private func setupExternalWindow(on screen: UIScreen) {
        // Reuse the existing external window if it exists
        if let existing = externalWindow {
            if existing.rootViewController == nil {
                existing.rootViewController = UIHostingController(
                    rootView: ExternalDisplayView(displayManager: self)
                )
            }
            // Always update frame to match external screen bounds
            existing.frame = screen.bounds
            existing.screen = screen
            existing.makeKeyAndVisible()
            print("External window reused: \(screen.bounds)")
            return
        }
        
        // Set the external screen to use its best available display mode (highest resolution)
        if let mode = screen.availableModes.max(by: { $0.size.width * $0.size.height < $1.size.width * $1.size.height }) {
            screen.currentMode = mode
            print("External screen set to mode: \(mode.size.width)×\(mode.size.height) @ \(mode.pixelAspectRatio)")
        }
        
        // Override screen settings to ensure fullscreen
        screen.overscanCompensation = .none // Disable overscan to use full screen
        
        // Modern iOS 13+ approach: Use window scene if available
        if #available(iOS 13.0, *) {
            if let windowScene = UIApplication.shared.connectedScenes
                .compactMap({ $0 as? UIWindowScene })
                .first(where: { $0.screen == screen }) {
                // Create window using window scene (modern API)
                let window = UIWindow(windowScene: windowScene)
                window.frame = screen.bounds
                window.screen = screen
                window.backgroundColor = .black
                window.windowLevel = .normal
                
                let hostingController = UIHostingController(
                    rootView: ExternalDisplayView(displayManager: self)
                )
                hostingController.view.frame = screen.bounds
                hostingController.view.backgroundColor = .black
                hostingController.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
                
                window.rootViewController = hostingController
                window.makeKeyAndVisible()
                
                externalWindow = window
                
                print("[ExternalDisplay] Window setup (WindowScene) complete:")
                print("  - Screen bounds: \(screen.bounds)")
                print("  - Screen scale: \(screen.scale)")
                print("  - Screen nativeScale: \(screen.nativeScale)")
                print("  - Screen nativeBounds: \(screen.nativeBounds)")
                print("  - Window frame: \(window.frame)")
                return
            }
        }
        
        // Fallback: Direct UIWindow creation with external screen (legacy API)
        let window = UIWindow(frame: screen.bounds)
        window.screen = screen
        window.backgroundColor = .black
        window.windowLevel = .normal
        
        let hostingController = UIHostingController(
            rootView: ExternalDisplayView(displayManager: self)
        )
        hostingController.view.frame = screen.bounds
        hostingController.view.backgroundColor = .black
        hostingController.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        
        window.rootViewController = hostingController
        window.makeKeyAndVisible()
        
        externalWindow = window
        
        print("[ExternalDisplay] Window setup (Direct) complete:")
        print("  - Screen bounds: \(screen.bounds)")
        print("  - Screen scale: \(screen.scale)")
        print("  - Screen nativeScale: \(screen.nativeScale)")
        print("  - Screen nativeBounds: \(screen.nativeBounds)")
        print("  - Window frame: \(window.frame)")
    }
    
    deinit {
        NotificationCenter.default.removeObserver(self)
    }
}

/// View shown on the external display (AirPlay/HDMI)
struct ExternalDisplayView: View {
    @ObservedObject var displayManager: ExternalDisplayManager
    
    var body: some View {
        GeometryReader { geometry in
            ZStack {
                Color.black
                    .ignoresSafeArea()
                
                // Metal view fills the entire external display
                ExternalMetalView()
                    .frame(width: geometry.size.width, height: geometry.size.height)
                    .position(x: geometry.size.width / 2, y: geometry.size.height / 2)
                
                // Optional: Show display mode indicator briefly
                VStack {
                    Spacer()
                    HStack {
                        Spacer()
                        VStack(alignment: .trailing, spacing: 4) {
                            Text(displayManager.displayMode.displayName)
                                .font(.system(size: 14, weight: .medium))
                            Text("External Display")
                                .font(.system(size: 12))
                        }
                        .foregroundStyle(.white.opacity(0.6))
                        .padding(.horizontal, 12)
                        .padding(.vertical, 8)
                        .background(.black.opacity(0.4), in: RoundedRectangle(cornerRadius: 8))
                        .padding(.trailing, 20)
                        .padding(.bottom, 20)
                    }
                }
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
        }
        .ignoresSafeArea(.all)
        .statusBarHidden(true)
        .persistentSystemOverlays(.hidden)
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
        
        // Use external screen's native scale and full bounds
        guard let window = window else {
            // Fallback to main screen if window not yet assigned
            metalLayer.contentsScale = UIScreen.main.scale
            metalLayer.drawableSize = CGSize(width: bounds.size.width * UIScreen.main.scale, 
                                             height: bounds.size.height * UIScreen.main.scale)
            return
        }
        
        let screen = window.screen
        
        // Use the external screen's native scale for full resolution
        let scale = screen.nativeScale
        metalLayer.contentsScale = scale
        
        // Set drawable size to match the full screen bounds at native scale
        let screenBounds = screen.bounds
        metalLayer.drawableSize = CGSize(width: screenBounds.size.width * scale, 
                                         height: screenBounds.size.height * scale)
        
        print("[ExternalDisplay] Metal layer setup: bounds=\(screenBounds), scale=\(scale), drawableSize=\(metalLayer.drawableSize)")
    }
    
    override func layoutSubviews() {
        super.layoutSubviews()
        
        // Ensure the layer fills the entire view bounds
        metalLayer.frame = bounds
        
        guard let window = window else {
            return
        }
        
        let screen = window.screen
        
        // Use native scale for maximum resolution
        let scale = screen.nativeScale
        metalLayer.contentsScale = scale
        
        // Update drawable size to match current bounds at native scale
        metalLayer.drawableSize = CGSize(width: bounds.size.width * scale, 
                                         height: bounds.size.height * scale)
        
        // Re-register the surface with the emulator if already set
        if isSurfaceSet {
            az_emu_secondary_surface_set(
                Unmanaged.passUnretained(metalLayer).toOpaque(),
                Float(scale)
            )
            print("[ExternalDisplay] Metal surface updated: drawableSize=\(metalLayer.drawableSize), scale=\(scale)")
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
