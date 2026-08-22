// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI
import QuartzCore
import UIKit

/// SwiftUI view that renders the appropriate 3DS screen(s) on the external display.
/// This is hosted inside the external UIWindow created by `DisplayManager`.
struct ExternalScreenView: View {
    @ObservedObject var displayManager: DisplayManager
    @State private var showingModeIndicator = true
    @State private var modeIndicatorTimer: Timer?

    var body: some View {
        GeometryReader { geometry in
            let size = geometry.size

            ZStack {
                Color.black.ignoresSafeArea()

                // The C++ renderer composites the correct screen(s) onto the
                // secondary Metal surface based on the C++ layout settings.
                // This SwiftUI view overlays a translucent HUD and handles
                // touch routing for the external display in modes that need it.

                ExternalMetalViewContainer()
                    .frame(width: size.width, height: size.height)
                    .position(x: size.width / 2, y: size.height / 2)

                // Mode indicator badge (auto-hides).
                if showingModeIndicator {
                    VStack {
                        Spacer()
                        HStack {
                            Spacer()
                            modeBadge
                        }
                    }
                    .padding(20)
                    .transition(.opacity)
                }

                // Touch overlay for external display touch input
                // (only active when the bottom screen is on the external display)
                if displayManager.configuration.bottomScreenDestination == .external {
                    ExternalTouchOverlay()
                        .allowsHitTesting(true)
                }
            }
            .frame(width: size.width, height: size.height)
        }
        .ignoresSafeArea(.all)
        .statusBarHidden(true)
        .persistentSystemOverlays(.hidden)
        .onAppear {
            showModeIndicator()
        }
        .onDisappear {
            modeIndicatorTimer?.invalidate()
        }
    }

    // MARK: - Mode Badge

    private var modeBadge: some View {
        VStack(alignment: .trailing, spacing: 4) {
            Text(displayManager.displayMode.displayName)
                .font(.system(size: 14, weight: .medium))
            Text("Azahar External Display")
                .font(.system(size: 12))
        }
        .foregroundStyle(.white.opacity(0.7))
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(.black.opacity(0.5), in: RoundedRectangle(cornerRadius: 8))
    }

    // MARK: - Mode Indicator

    private func showModeIndicator() {
        showingModeIndicator = true
        modeIndicatorTimer?.invalidate()
        modeIndicatorTimer = Timer.scheduledTimer(withTimeInterval: 3.0, repeats: false) { _ in
            withAnimation(.easeOut(duration: 0.5)) {
                showingModeIndicator = false
            }
        }
    }
}

// MARK: - External Metal View Container

/// UIViewRepresentable that hosts a CAMetalLayer on the external display.
/// The C++ renderer renders to this layer based on the secondary_display_layout setting.
struct ExternalMetalViewContainer: UIViewRepresentable {
    func makeUIView(context: Context) -> ExternalMetalContainerView {
        ExternalMetalContainerView()
    }

    func updateUIView(_ uiView: ExternalMetalContainerView, context: Context) {
        uiView.updateSurfaceIfNeeded()
    }
}

/// UIKit view that hosts the Metal layer for the external display.
/// The C++ side drives rendering to this surface via az_emu_secondary_surface_set().
final class ExternalMetalContainerView: UIView {
    private var isSurfaceConfigured = false

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError()
    }

    override class var layerClass: AnyClass {
        CAMetalLayer.self
    }

    func updateSurfaceIfNeeded() {
        guard !isSurfaceConfigured else { return }
        configureSurface()
    }

    private func configureSurface() {
        guard let window = window else {
            // Will be called again after the view is in a window.
            return
        }

        let screen = window.screen
        let layer = self.layer as! CAMetalLayer
        layer.device = MTLCreateSystemDefaultDevice()
        layer.pixelFormat = .bgra8Unorm
        layer.framebufferOnly = false
        layer.contentsScale = screen.nativeScale
        layer.drawableSize = CGSize(
            width: bounds.width * screen.nativeScale,
            height: bounds.height * screen.nativeScale
        )

        let scale = Float(screen.nativeScale)
        az_emu_secondary_surface_set(
            Unmanaged.passUnretained(layer).toOpaque(),
            scale
        )
        isSurfaceConfigured = true

        AppLogger.info("[ExternalMetal] Surface configured: \(layer.drawableSize)")
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        guard isSurfaceConfigured, let window = window else { return }

        let screen = window.screen
        let layer = self.layer as! CAMetalLayer
        layer.frame = bounds
        layer.contentsScale = screen.nativeScale
        layer.drawableSize = CGSize(
            width: bounds.width * screen.nativeScale,
            height: bounds.height * screen.nativeScale
        )

        // Re-register with the C++ renderer.
        let scale = Float(screen.nativeScale)
        az_emu_secondary_surface_set(
            Unmanaged.passUnretained(layer).toOpaque(),
            scale
        )
    }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window != nil {
            configureSurface()
        }
    }
}

// MARK: - External Touch Overlay

/// Transparent overlay that routes touch events to the C++ bridge
/// when the bottom screen is on the external display.
struct ExternalTouchOverlay: UIViewRepresentable {
    func makeUIView(context: Context) -> ExternalTouchView {
        let view = ExternalTouchView()
        view.backgroundColor = .clear
        return view
    }

    func updateUIView(_ uiView: ExternalTouchView, context: Context) {}
}

/// UIView subclass that handles touch events and routes them to the secondary
/// window's touch handler via the C bridge.
class ExternalTouchView: UIView {
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }
        let location = touch.location(in: self)
        let scale = window?.screen.scale ?? UIScreen.main.scale
        let pixelX = Float(location.x * scale)
        let pixelY = Float(location.y * scale)
        az_secondary_touch_event(pixelX, pixelY, true)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }
        let location = touch.location(in: self)
        let scale = window?.screen.scale ?? UIScreen.main.scale
        let pixelX = Float(location.x * scale)
        let pixelY = Float(location.y * scale)
        az_secondary_touch_moved(pixelX, pixelY)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }
        let location = touch.location(in: self)
        let scale = window?.screen.scale ?? UIScreen.main.scale
        let pixelX = Float(location.x * scale)
        let pixelY = Float(location.y * scale)
        az_secondary_touch_event(pixelX, pixelY, false)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }
        let location = touch.location(in: self)
        let scale = window?.screen.scale ?? UIScreen.main.scale
        let pixelX = Float(location.x * scale)
        let pixelY = Float(location.y * scale)
        az_secondary_touch_event(pixelX, pixelY, false)
    }
}
