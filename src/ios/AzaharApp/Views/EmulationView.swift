// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI

/// Emulation host view: shows the MetalView, touch overlay, and pause/menu controls.
struct EmulationView: View {
    @EnvironmentObject var appState: AppState
    @StateObject private var viewModel: EmulationViewModel
    @StateObject private var gamepadManager = GamepadManager()
    @StateObject private var touchControlSettings = TouchControlSettings.shared
    @State private var showPauseMenu = false
    @State private var showAmiiboPicker = false
    @State private var isLandscape = UIDevice.current.orientation.isLandscape
    @State private var orientationObserver: NSObjectProtocol?
    @State private var showOverlayButtons = true
    @State private var overlayButtonsTimer: Timer?

    let game: Game

    init(game: Game) {
        self.game = game
        _viewModel = StateObject(wrappedValue: EmulationViewModel(game: game))
        
        AppLogger.info("=== EMULATION VIEW INITIALIZED ===")
        AppLogger.gameOperation("EmulationView created", path: game.path, titleId: game.titleId)
    }
    
    /// Reset both joysticks to neutral. Called on orientation change
    /// to prevent stuck deflection from cancelled DragGestures.
    private func resetJoysticks() {
        az_analog_event(Int32(AZ_STICK_LEFT), 0, 0)
        az_analog_event(Int32(AZ_STICK_C), 0, 0)
        viewModel.leftStickPosition = .zero
        viewModel.rightStickPosition = .zero
        AppLogger.debug("[EmulationView] Joysticks reset on orientation change")
    }

    var body: some View {
        ZStack {
            // Main emulation view - use safe area to avoid notch/Dynamic Island
            GeometryReader { geometry in
                MetalView(viewModel: viewModel, safeArea: geometry.safeAreaInsets)
                    .overlay {
                        // 3DS bottom screen touch overlay - ALWAYS enabled for touch even with controller
                        // This overlay only handles 3DS touch screen (not buttons)
                        TouchScreenOverlay(viewModel: viewModel, geometry: geometry)
                            .allowsHitTesting(true)
                    }
                    .overlay {
                        // Touch controls visibility
                        // When visible, controls are above the touch screen overlay
                        if viewModel.isControlsVisible {
                            TouchControlsView(viewModel: viewModel)
                                .allowsHitTesting(true)
                        }
                    }
            }
            
            // Tap detector overlay for showing hidden buttons (BEHIND touch screen and controls)
            // Only active when overlay buttons are hidden AND controls are hidden
            if !showOverlayButtons && !viewModel.isControlsVisible {
                Color.clear
                    .contentShape(Rectangle())
                    .onTapGesture {
                        showOverlayButtons = true
                        resetOverlayButtonsTimer()
                    }
                    .allowsHitTesting(true)
                    .zIndex(-1)  // Behind everything - only catches taps that don't hit touch screen/controls
            }
            
            // Loading screen overlay
            if viewModel.isLoading {
                EmulationLoadingView(
                    gameTitle: viewModel.gameTitle,
                    gamePath: game.path
                )
                .transition(.opacity)
                .zIndex(100)
            }

            // Top bar overlay (auto-hides)
            VStack {
                HStack {
                    Button {
                        viewModel.togglePause()
                        showPauseMenu = true
                    } label: {
                        Image(systemName: "pause.fill")
                            .font(.title2)
                            .foregroundStyle(.white)
                            .padding(8)
                            .background(.ultraThinMaterial, in: Circle())
                    }
                    
                    // Toggle controls visibility button
                    Button {
                        viewModel.isControlsVisible.toggle()
                    } label: {
                        Image(systemName: viewModel.isControlsVisible ? "gamecontroller.fill" : "gamecontroller")
                            .font(.title2)
                            .foregroundStyle(.white)
                            .padding(8)
                            .background(.ultraThinMaterial, in: Circle())
                    }
                    
                    Spacer()
                    
                    // Performance stats
                    if viewModel.showPerfStats {
                        Text(viewModel.perfStatsText)
                            .font(.caption2.monospaced())
                            .foregroundStyle(.white)
                            .padding(4)
                            .background(.black.opacity(0.6), in: RoundedRectangle(cornerRadius: 4))
                    }
                }
                .padding(.horizontal, 16)
                .padding(.top, 8)
                Spacer()
                    .allowsHitTesting(false)  // Let touches pass through to game screen and controls below
            }
            .allowsHitTesting(showOverlayButtons)  // Only intercept touches when buttons are visible

            // Pause menu
            if showPauseMenu {
                PauseMenuView(
                    viewModel: viewModel,
                    onResume: {
                        viewModel.resume()
                        showPauseMenu = false
                    },
                    onExit: {
                        viewModel.stop()
                        appState.stopEmulation()
                    },
                    onLoadAmiibo: {
                        showAmiiboPicker = true
                    }
                )
            }
        }
        .sheet(isPresented: $showAmiiboPicker) {
            AmiiboFilePicker(isPresented: $showAmiiboPicker) { url in
                viewModel.loadAmiiboFile(url: url)
            }
        }

        .onAppear {
            AppLogger.info("[EmulationView] onAppear - starting emulation")
            
            // Keep screen awake during emulation
            UIApplication.shared.isIdleTimerDisabled = true
            
            // Start overlay button auto-hide timer
            resetOverlayButtonsTimer()
            
            // Set up orientation observer
            orientationObserver = NotificationCenter.default.addObserver(
                forName: UIDevice.orientationDidChangeNotification,
                object: nil,
                queue: .main
            ) { _ in
                let newOrientation = UIDevice.current.orientation
                if newOrientation.isLandscape || newOrientation == .portrait {
                    isLandscape = newOrientation.isLandscape
                    AppLogger.debug("[EmulationView] Orientation changed: \(newOrientation.isLandscape ? "landscape" : "portrait")")
                    
                    // Notify C++ side about orientation change for framebuffer layout
                    let isPortrait = !newOrientation.isLandscape
                    az_update_framebuffer(isPortrait)
                    
                    resetJoysticks()
                }
            }
            
            viewModel.startEmulation()
        }
        .onDisappear {
            AppLogger.info("[EmulationView] onDisappear - stopping emulation")
            
            // Re-enable screen sleep when leaving emulation
            UIApplication.shared.isIdleTimerDisabled = false
            
            // Clean up timer
            overlayButtonsTimer?.invalidate()
            overlayButtonsTimer = nil
            
            if let observer = orientationObserver {
                NotificationCenter.default.removeObserver(observer)
                orientationObserver = nil
            }
            viewModel.stop()
        }
    }
    
    // MARK: - Helper Functions
    
    /// Reset overlay buttons auto-hide timer
    /// Buttons hide after 5 seconds if touch controls are disabled
    private func resetOverlayButtonsTimer() {
        overlayButtonsTimer?.invalidate()
        showOverlayButtons = true
        
        // Only auto-hide if touch controls are disabled
        if !viewModel.isControlsVisible {
            overlayButtonsTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: false) { _ in
                withAnimation(.easeOut(duration: 0.3)) {
                    showOverlayButtons = false
                }
            }
        }
    }
}

/// Pause menu (equivalent to Android's EmulationMenuDialog).
struct PauseMenuView: View {
    @ObservedObject var viewModel: EmulationViewModel
    @State private var showCheatsView = false
    @State private var showSettingsView = false
    @State private var showSaveStateDialog = false
    @State private var showLoadStateDialog = false
    let onResume: () -> Void
    let onExit: () -> Void
    let onLoadAmiibo: () -> Void

    var body: some View {
        ZStack {
            Color.black.opacity(0.7)
                .ignoresSafeArea()
                .onTapGesture { onResume() }

            VStack(spacing: 16) {
                HStack {
                    Text(gameTitle)
                        .font(.title2.bold())
                        .foregroundStyle(.white)
                    Spacer()
                    Button {
                        onResume()
                    } label: {
                        Image(systemName: "xmark.circle.fill")
                            .font(.title2)
                            .foregroundStyle(.white.opacity(0.8))
                    }
                }
                .padding(.bottom, 8)

                ScrollView(.vertical, showsIndicators: false) {
                    VStack(spacing: 10) {
                        // Resume
                        PauseButton(title: "Resume", icon: "play.fill", action: onResume)
                        
                        Divider().background(Color.white.opacity(0.3))
                        
                        // Save State
                        PauseButton(title: "Save State", icon: "square.and.arrow.down.fill") {
                            showSaveStateDialog = true
                        }
                        
                        // Load State
                        PauseButton(title: "Load State", icon: "square.and.arrow.up.fill") {
                            showLoadStateDialog = true
                        }
                        
                        Divider().background(Color.white.opacity(0.3))
                        
                        // Swap Screens
                        PauseButton(title: "Swap Screens", icon: "rectangle.2.swap") {
                            viewModel.swapScreens()
                            // Don't close menu - let user see immediate change
                        }
                        
                        // Cycle Layout
                        PauseButton(title: "Change Layout", icon: "rectangle.split.3x1") {
                            viewModel.cycleLayout()
                            // Don't close menu - let user see immediate change
                        }
                        
                        // Toggle Turbo
                        PauseButton(
                            title: viewModel.turboEnabled ? "Turbo: ON" : "Turbo: OFF",
                            icon: "bolt.fill"
                        ) {
                            viewModel.toggleTurbo()
                        }
                        .tint(viewModel.turboEnabled ? .yellow : .gray)
                        
                        // Edit Controls
                        PauseButton(title: "Edit Touch Controls", icon: "hand.tap.fill") {
                            viewModel.toggleEditControls()
                            onResume()
                        }
                        
                        Divider().background(Color.white.opacity(0.3))
                        
                        // Cheats
                        PauseButton(title: "Cheats", icon: "sparkles") {
                            showCheatsView = true
                        }
                        
                        // Settings
                        PauseButton(title: "Settings", icon: "gearshape.fill") {
                            showSettingsView = true
                        }
                        
                        // Amiibo
                        PauseButton(title: "Load Amiibo", icon: "circle.grid.2x2.fill") {
                            onLoadAmiibo()
                        }
                        
                        // Screenshot
                        PauseButton(title: "Take Screenshot", icon: "camera.fill") {
                            viewModel.takeScreenshot()
                            onResume()
                        }
                        
                        // Reset
                        PauseButton(title: "Reset Game", icon: "arrow.clockwise") {
                            viewModel.resetGame()
                            onResume()
                        }
                        .tint(.orange)
                        
                        Divider().background(Color.white.opacity(0.3))
                        
                        // Performance Stats
                        PauseButton(
                            title: viewModel.showPerfStats ? "Hide Stats" : "Show Stats",
                            icon: "chart.bar.fill"
                        ) {
                            viewModel.togglePerfStats()
                        }
                        
                        Divider().background(Color.white.opacity(0.3))

                        // Exit
                        PauseButton(title: "Exit Game", icon: "xmark.circle.fill", action: onExit)
                            .tint(.red)
                    }
                    .padding(.horizontal)
                }
                .frame(maxHeight: 500)
            }
            .frame(maxWidth: 450)
            .padding(24)
            .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 20))
            .padding(.horizontal, 20)
            
            // Save State Dialog
            if showSaveStateDialog {
                SaveStateDialog(
                    viewModel: viewModel,
                    isPresented: $showSaveStateDialog,
                    isSaving: true
                )
            }
            
            // Load State Dialog
            if showLoadStateDialog {
                SaveStateDialog(
                    viewModel: viewModel,
                    isPresented: $showLoadStateDialog,
                    isSaving: false
                )
            }
        }
        .transition(.opacity)
        .sheet(isPresented: $showCheatsView) {
            CheatsView()
        }
        .sheet(isPresented: $showSettingsView) {
            PerGameSettingsView(game: viewModel.game)
        }
    }

    private var gameTitle: String {
        viewModel.gameTitle
    }
}

struct PauseButton: View {
    let title: String
    let icon: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Label(title, systemImage: icon)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 8)
        }
        .buttonStyle(.bordered)
    }
}

/// Transparent overlay for 3DS bottom screen touch input
/// Maps touches from screen coordinates to 3DS bottom screen framebuffer coordinates
struct TouchScreenOverlay: View {
    @ObservedObject var viewModel: EmulationViewModel
    let geometry: GeometryProxy
    @State private var touchIndicator: CGPoint?
    @State private var touchIndicatorTimer: Timer?
    
    var body: some View {
        ZStack {
            // Use UIKit-based touch handling for reliable tap and drag detection
            TouchRepresentable(
                onTouchBegan: { location in
                    handleTouch(at: location, pressed: true)
                    showTouchIndicator(at: location)
                },
                onTouchMoved: { location in
                    handleTouch(at: location, pressed: true)
                    showTouchIndicator(at: location)
                },
                onTouchEnded: { location in
                    handleTouch(at: location, pressed: false)
                    az_touch_event(0, 0, false)
                    hideTouchIndicator()
                }
            )
            
            // Visual touch indicator (small circle that follows your finger)
            if let position = touchIndicator {
                Circle()
                    .fill(Color.white.opacity(0.5))
                    .frame(width: 30, height: 30)
                    .overlay(
                        Circle()
                            .stroke(Color.blue, lineWidth: 2)
                    )
                    .position(position)
                    .allowsHitTesting(false)
                    .transition(.opacity)
            }
        }
    }
    
    private func handleTouch(at location: CGPoint, pressed: Bool) {
        // Get the actual screen bounds and scale
        let scale = UIScreen.main.scale
        let screenWidth = geometry.size.width
        let screenHeight = geometry.size.height
        
        // Calculate framebuffer pixel coordinates
        // The Metal drawable uses pixel coordinates (points * scale)
        let pixelX = Float(location.x * scale)
        let pixelY = Float(location.y * scale)
        
        // Determine if we're in landscape or portrait
        let isLandscape = screenWidth > screenHeight
        
        // Log detailed touch information
        if pressed {
            AppLogger.debug("[TouchScreenOverlay] Touch DOWN:")
            AppLogger.debug("  Screen coords: (\(location.x), \(location.y))")
            AppLogger.debug("  Pixel coords: (\(pixelX), \(pixelY))")
            AppLogger.debug("  Screen size: \(screenWidth)x\(screenHeight)")
            AppLogger.debug("  Scale: \(scale)")
            AppLogger.debug("  Orientation: \(isLandscape ? "Landscape" : "Portrait")")
        }
        
        // Send to emulator - the C++ side will:
        // 1. Check if coordinates are within bottom_screen bounds
        // 2. Map to 3DS touch coordinates (0-320 x 0-240)
        let result = az_touch_event(pixelX, pixelY, pressed)
        
        if !result && pressed {
            AppLogger.debug("[TouchScreenOverlay] Touch rejected - outside bottom screen area")
        }
        
        if pressed {
            az_touch_moved(pixelX, pixelY)
        }
    }
    
    private func showTouchIndicator(at position: CGPoint) {
        withAnimation(.easeInOut(duration: 0.1)) {
            touchIndicator = position
        }
        
        // Auto-hide after 2 seconds of inactivity
        touchIndicatorTimer?.invalidate()
        touchIndicatorTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: false) { _ in
            hideTouchIndicator()
        }
    }
    
    private func hideTouchIndicator() {
        withAnimation(.easeOut(duration: 0.2)) {
            touchIndicator = nil
        }
        touchIndicatorTimer?.invalidate()
        touchIndicatorTimer = nil
    }
}

/// UIKit-based touch handler for reliable touch detection
/// SwiftUI's DragGesture can be unreliable for quick taps
struct TouchRepresentable: UIViewRepresentable {
    let onTouchBegan: (CGPoint) -> Void
    let onTouchMoved: (CGPoint) -> Void
    let onTouchEnded: (CGPoint) -> Void
    
    func makeUIView(context: Context) -> TouchView {
        let view = TouchView()
        view.backgroundColor = .clear
        view.onTouchBegan = onTouchBegan
        view.onTouchMoved = onTouchMoved
        view.onTouchEnded = onTouchEnded
        return view
    }
    
    func updateUIView(_ uiView: TouchView, context: Context) {
        uiView.onTouchBegan = onTouchBegan
        uiView.onTouchMoved = onTouchMoved
        uiView.onTouchEnded = onTouchEnded
    }
}

/// UIView subclass that handles touch events directly
class TouchView: UIView {
    var onTouchBegan: ((CGPoint) -> Void)?
    var onTouchMoved: ((CGPoint) -> Void)?
    var onTouchEnded: ((CGPoint) -> Void)?
    
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }
        let location = touch.location(in: self)
        onTouchBegan?(location)
    }
    
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }
        let location = touch.location(in: self)
        onTouchMoved?(location)
    }
    
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }
        let location = touch.location(in: self)
        onTouchEnded?(location)
    }
    
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }
        let location = touch.location(in: self)
        onTouchEnded?(location)
    }
}
