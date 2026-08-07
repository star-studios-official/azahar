// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import UIKit
import SwiftUI
import QuartzCore
import Metal

/// Manages external displays (AirPlay, HDMI, USB-C) for dual-screen 3DS emulation
class ExternalDisplayManager: ObservableObject {
    static let shared = ExternalDisplayManager()
    
    @Published var isExternalDisplayConnected = false
    @Published var externalScreen: UIScreen?
    @Published var externalWindow: UIWindow?
    @Published var displayMode: ExternalDisplayMode = .topScreenExternal
    @Published var showMirrorModeAlert = false
    
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
        // Log initialization
        AppLogger.info("[ExternalDisplay] Initializing ExternalDisplayManager")
        AppLogger.info("[ExternalDisplay] Current screens count: \(UIScreen.screens.count)")
        
        // Log all available screens
        for (index, screen) in UIScreen.screens.enumerated() {
            AppLogger.info("[ExternalDisplay] Screen[\(index)]: bounds=\(screen.bounds), scale=\(screen.scale), nativeScale=\(screen.nativeScale)")
            if #available(iOS 13.0, *) {
                AppLogger.info("[ExternalDisplay] Screen[\(index)]: mirrored=\(screen.mirrored != nil)")
            }
        }
        
        setupScreenNotifications()
        checkForExternalDisplay()
    }
    
    private func setupScreenNotifications() {
        // Monitor for external display connection/disconnection
        AppLogger.info("[ExternalDisplay] Setting up screen notifications")
        
        // Modern iOS 13+ scene-based notifications (posted by ExternalSceneDelegate)
        NotificationCenter.default.addObserver(
            forName: Notification.Name("ExternalSceneConnected"),
            object: nil,
            queue: .main
        ) { [weak self] notification in
            AppLogger.info("[ExternalDisplay] *** EXTERNAL SCENE CONNECTED NOTIFICATION ***")
            
            guard let screen = notification.object as? UIScreen else {
                AppLogger.error("ExternalDisplay", message: "Failed to cast notification object to UIScreen")
                return
            }
            
            if let window = notification.userInfo?["window"] as? UIWindow {
                AppLogger.info("[ExternalDisplay] Received external window from scene delegate")
                self?.externalWindow = window
            }
            
            AppLogger.info("[ExternalDisplay] Connected screen via scene delegate:")
            AppLogger.info("[ExternalDisplay]   - Bounds: \(screen.bounds)")
            AppLogger.info("[ExternalDisplay]   - Scale: \(screen.scale)")
            
            self?.handleExternalDisplayConnected(screen)
        }
        
        NotificationCenter.default.addObserver(
            forName: Notification.Name("ExternalSceneDisconnected"),
            object: nil,
            queue: .main
        ) { [weak self] notification in
            AppLogger.info("[ExternalDisplay] *** EXTERNAL SCENE DISCONNECTED NOTIFICATION ***")
            self?.handleExternalDisplayDisconnected()
        }
        
        NotificationCenter.default.addObserver(
            forName: Notification.Name("ExternalDisplayViewReady"),
            object: nil,
            queue: .main
        ) { [weak self] notification in
            AppLogger.info("[ExternalDisplay] *** EXTERNAL DISPLAY VIEW READY ***")
            
            guard let viewController = notification.object as? UIViewController else {
                AppLogger.error("ExternalDisplay", message: "Failed to get view controller from notification")
                return
            }
            
            AppLogger.info("[ExternalDisplay] External display view is ready, bounds: \(viewController.view.bounds)")
            
            // Set up Metal rendering surface for external display
            self?.setupSecondaryMetalSurface(on: viewController.view)
        }
        
        // Legacy UIScreen notifications (for older iOS or non-scene based connections)
        NotificationCenter.default.addObserver(
            forName: UIScreen.didConnectNotification,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            AppLogger.info("[ExternalDisplay] *** SCREEN CONNECTED NOTIFICATION (LEGACY) ***")
            AppLogger.info("[ExternalDisplay] Notification object: \(String(describing: notification.object))")
            
            guard let screen = notification.object as? UIScreen else {
                AppLogger.error("ExternalDisplay", message: "Failed to cast notification object to UIScreen")
                return
            }
            
            AppLogger.info("[ExternalDisplay] Connected screen details:")
            AppLogger.info("[ExternalDisplay]   - Bounds: \(screen.bounds)")
            AppLogger.info("[ExternalDisplay]   - Scale: \(screen.scale)")
            AppLogger.info("[ExternalDisplay]   - Native scale: \(screen.nativeScale)")
            AppLogger.info("[ExternalDisplay]   - Native bounds: \(screen.nativeBounds)")
            AppLogger.info("[ExternalDisplay]   - Current mode: \(String(describing: screen.currentMode))")
            AppLogger.info("[ExternalDisplay]   - Available modes count: \(screen.availableModes.count)")
            
            for (index, mode) in screen.availableModes.enumerated() {
                AppLogger.info("[ExternalDisplay]   - Mode[\(index)]: size=\(mode.size), pixelAspectRatio=\(mode.pixelAspectRatio)")
            }
            
            if #available(iOS 13.0, *) {
                // Log window scenes
                let scenes = UIApplication.shared.connectedScenes
                AppLogger.info("[ExternalDisplay] Connected scenes count: \(scenes.count)")
                for scene in scenes {
                    if let windowScene = scene as? UIWindowScene {
                        AppLogger.info("[ExternalDisplay] WindowScene: screen=\(windowScene.screen.bounds), windows=\(windowScene.windows.count)")
                    }
                }
            }
            
            self?.handleExternalDisplayConnected(screen)
        }
        
        NotificationCenter.default.addObserver(
            forName: UIScreen.didDisconnectNotification,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            AppLogger.info("[ExternalDisplay] *** SCREEN DISCONNECTED NOTIFICATION (LEGACY) ***")
            AppLogger.info("[ExternalDisplay] Notification object: \(String(describing: notification.object))")
            self?.handleExternalDisplayDisconnected()
        }
        
        AppLogger.info("[ExternalDisplay] Screen notifications setup complete")
    }
    
    private func checkForExternalDisplay() {
        // Check if there's already an external screen connected
        AppLogger.info("[ExternalDisplay] Checking for existing external displays")
        AppLogger.info("[ExternalDisplay] UIScreen.screens.count = \(UIScreen.screens.count)")
        
        if UIScreen.screens.count > 1 {
            let externalScreen = UIScreen.screens[1]
            AppLogger.info("[ExternalDisplay] Found existing external screen at index 1")
            AppLogger.info("[ExternalDisplay] External screen bounds: \(externalScreen.bounds)")
            AppLogger.info("[ExternalDisplay] External screen scale: \(externalScreen.scale)")
            handleExternalDisplayConnected(externalScreen)
        } else {
            AppLogger.info("[ExternalDisplay] No true external display detected (UIScreen.screens.count == 1)")
            
            // Check if main screen is being mirrored (common with HDMI adapters)
            // We can still use the mirrored screen for rendering!
            if #available(iOS 13.0, *) {
                if let mirroredScreen = UIScreen.main.mirrored {
                    AppLogger.info("[ExternalDisplay] Found mirrored screen: \(mirroredScreen.bounds)")
                    AppLogger.info("[ExternalDisplay] Mirrored screen scale: \(mirroredScreen.scale)")
                    AppLogger.info("[ExternalDisplay] Attempting to use mirrored screen for video output...")
                    
                    // Treat the mirrored screen as an external display
                    // This allows HDMI adapters to work for full-screen video output
                    handleExternalDisplayConnected(mirroredScreen)
                    
                    AppLogger.info("[ExternalDisplay] Successfully initialized mirrored screen rendering")
                    AppLogger.info("[ExternalDisplay] Note: This is a mirrored display (HDMI adapter), not true dual-screen")
                } else {
                    AppLogger.info("[ExternalDisplay] Main screen is not mirrored - no external display connected")
                }
            }
        }
    }
    
    func handleExternalDisplayConnected(_ screen: UIScreen) {
        AppLogger.info("[ExternalDisplay] ===== handleExternalDisplayConnected START =====")
        AppLogger.info("[ExternalDisplay] Screen bounds: \(screen.bounds)")
        AppLogger.info("[ExternalDisplay] Screen scale: \(screen.scale)")
        AppLogger.info("[ExternalDisplay] Screen nativeScale: \(screen.nativeScale)")
        AppLogger.info("[ExternalDisplay] Screen nativeBounds: \(screen.nativeBounds)")
        
        externalScreen = screen
        isExternalDisplayConnected = true
        
        AppLogger.info("[ExternalDisplay] Set isExternalDisplayConnected = true")
        
        // Set preferred display mode for best quality
        if let mode = screen.availableModes.max(by: { $0.size.width < $1.size.width }) {
            AppLogger.info("[ExternalDisplay] Setting screen to best mode: \(mode.size.width)×\(mode.size.height)")
            screen.currentMode = mode
            AppLogger.info("[ExternalDisplay] Screen mode set successfully")
        } else {
            AppLogger.warning("ExternalDisplay", message: "No available modes found for external screen")
        }
        
        // ManicEMU-style: Auto-detect and default to fullscreen mode
        let savedMode = UserDefaults.standard.integer(forKey: "external_display_mode")
        let hasSetMode = UserDefaults.standard.bool(forKey: "has_set_external_display_mode")
        
        AppLogger.info("[ExternalDisplay] Saved display mode: \(savedMode), hasSetMode: \(hasSetMode)")
        
        if savedMode == 0 && !hasSetMode {
            // First-time external display connection - auto-set to fullscreen
            displayMode = .externalFullscreen
            UserDefaults.standard.set(ExternalDisplayMode.externalFullscreen.rawValue, forKey: "external_display_mode")
            UserDefaults.standard.set(true, forKey: "has_set_external_display_mode")
            AppLogger.info("[ExternalDisplay] Auto-configured to fullscreen mode (first connection)")
        } else {
            displayMode = ExternalDisplayMode(rawValue: savedMode) ?? .externalFullscreen
            AppLogger.info("[ExternalDisplay] Loaded saved mode: \(displayMode.displayName)")
        }
        
        // Create the external window if the scene delegate hasn't done it already.
        AppLogger.info("[ExternalDisplay] Creating/updating external window...")
        setupExternalWindow(on: screen)
        
        // Apply the display mode
        AppLogger.info("[ExternalDisplay] Applying display mode: \(displayMode.displayName)")
        applyDisplayMode()
        
        // Notify orientation-locking controllers to re-evaluate
        NotificationCenter.default.post(name: Notification.Name("ExternalDisplayModeChanged"), object: nil)
        AppLogger.info("[ExternalDisplay] Posted ExternalDisplayModeChanged notification")
        
        AppLogger.info("[ExternalDisplay] ===== handleExternalDisplayConnected END =====")
    }
    
    func handleExternalDisplayDisconnected() {
        AppLogger.info("[ExternalDisplay] ===== handleExternalDisplayDisconnected START =====")
        
        // Clean up external window
        if let window = externalWindow {
            AppLogger.info("[ExternalDisplay] Cleaning up external window: \(window.frame)")
            window.isHidden = true
            externalWindow = nil
        } else {
            AppLogger.info("[ExternalDisplay] No external window to clean up")
        }
        
        externalScreen = nil
        isExternalDisplayConnected = false
        
        AppLogger.info("[ExternalDisplay] Set isExternalDisplayConnected = false")
        
        // Destroy secondary surface
        az_emu_secondary_surface_destroy()
        AppLogger.info("[ExternalDisplay] Destroyed secondary surface")
        
        // Notify orientation-locking controllers to re-evaluate
        NotificationCenter.default.post(name: Notification.Name("ExternalDisplayModeChanged"), object: nil)
        AppLogger.info("[ExternalDisplay] Posted ExternalDisplayModeChanged notification")
        
        AppLogger.info("[ExternalDisplay] ===== handleExternalDisplayDisconnected END =====")
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
    
    /// Force external display initialization manually (like ManicEMU)
    /// Creates external window even if no external screen detected
    /// Useful for AirPlay/HDMI that may not trigger automatic detection
    func forceExternalDisplay() {
        AppLogger.info("[ExternalDisplay] Force External Display requested")
        
        // Check if already connected
        if isExternalDisplayConnected {
            AppLogger.info("[ExternalDisplay] External display already connected, reapplying mode")
            applyDisplayMode()
            return
        }
        
        // Try to find any external screen
        if UIScreen.screens.count > 1 {
            let screen = UIScreen.screens[1]
            AppLogger.info("[ExternalDisplay] Found external screen via force: \(screen.bounds)")
            handleExternalDisplayConnected(screen)
            return
        }
        
        // No external screen found, but try to initialize AirPlay/HDMI anyway
        // This handles cases where iOS doesn't properly report external screens
        AppLogger.info("[ExternalDisplay] No external screen detected, attempting manual initialization")
        
        // Check for available window scenes with external screens
        if #available(iOS 13.0, *) {
            for scene in UIApplication.shared.connectedScenes {
                if let windowScene = scene as? UIWindowScene {
                    if windowScene.screen != UIScreen.main {
                        AppLogger.info("[ExternalDisplay] Found external window scene: \(windowScene.screen.bounds)")
                        handleExternalDisplayConnected(windowScene.screen)
                        return
                    }
                }
            }
        }
        
        AppLogger.warning("ExternalDisplay", message: "Force external display failed - no external screen or window scene found. Please ensure AirPlay/HDMI is properly connected.")
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
    
    /// Set up Metal rendering surface for external display view controller
    /// Called when ExternalDisplayViewController's view is ready
    private func setupSecondaryMetalSurface(on view: UIView) {
        AppLogger.info("[ExternalDisplay] Setting up secondary Metal surface")
        AppLogger.info("[ExternalDisplay] View bounds: \(view.bounds)")
        AppLogger.info("[ExternalDisplay] View frame: \(view.frame)")
        
        // The view should already have a layer - use it for Metal rendering
        let metalLayer = CAMetalLayer()
        metalLayer.frame = view.bounds
        metalLayer.device = MTLCreateSystemDefaultDevice()
        metalLayer.framebufferOnly = false
        metalLayer.contentsScale = view.window?.screen.nativeScale ?? UIScreen.main.nativeScale
        
        // Add Metal layer to the view
        view.layer.addSublayer(metalLayer)
        
        AppLogger.info("[ExternalDisplay] Metal layer configured:")
        AppLogger.info("[ExternalDisplay]   - Frame: \(metalLayer.frame)")
        AppLogger.info("[ExternalDisplay]   - Drawable size: \(metalLayer.drawableSize)")
        AppLogger.info("[ExternalDisplay]   - Contents scale: \(metalLayer.contentsScale)")
        
        // Set the secondary surface in the C++ emulator core
        let scale = Float(metalLayer.contentsScale)
        
        AppLogger.info("[ExternalDisplay] Setting secondary surface with scale: \(scale)")
        az_emu_secondary_surface_set(
            Unmanaged.passUnretained(metalLayer).toOpaque(),
            scale
        )
        
        AppLogger.info("[ExternalDisplay] Secondary Metal surface setup complete")
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
