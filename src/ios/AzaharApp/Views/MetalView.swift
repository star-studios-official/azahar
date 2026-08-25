// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI
import QuartzCore
import UIKit

/// A UIView that hosts a CAMetalLayer and drives the emulation present loop
/// via a CADisplayLink. This is the SwiftUI-side rendering surface.
struct MetalView: UIViewRepresentable {
    @ObservedObject var viewModel: EmulationViewModel
    var safeArea: EdgeInsets

    func makeUIView(context: Context) -> MetalViewUIView {
        AppLogger.info("[MetalView] Creating MetalViewUIView")
        let view = MetalViewUIView(viewModel: viewModel)
        return view
    }

    func updateUIView(_ uiView: MetalViewUIView, context: Context) {
        uiView.viewModel = viewModel
        uiView.updateSafeArea(safeArea)
    }
}

/// UIKit view backing the MetalView SwiftUI wrapper.
/// Creates a CAMetalLayer and hands it to the bridge.
final class MetalViewUIView: UIView {
    var viewModel: EmulationViewModel
    private var customSafeAreaInsets: EdgeInsets = EdgeInsets()

    private var displayLink: CADisplayLink?
    private var isSurfaceSet = false

    init(viewModel: EmulationViewModel) {
        self.viewModel = viewModel
        super.init(frame: .zero)
        setupLayer()
    }
    
    func updateSafeArea(_ insets: EdgeInsets) {
        guard insets != customSafeAreaInsets else { return }
        customSafeAreaInsets = insets
        if isSurfaceSet {
            updateLayout()
        }
    }

    @available(*, unavailable)
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
        let scale = UIScreen.main.scale
        metalLayer.contentsScale = scale
        metalLayer.drawableSize = CGSize(width: bounds.size.width * scale, height: bounds.size.height * scale)
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        
        // Metal layer fills entire bounds
        metalLayer.frame = bounds
        
        // Calculate content rect accounting for safe area
        let contentWidth = bounds.width - customSafeAreaInsets.leading - customSafeAreaInsets.trailing
        let contentHeight = bounds.height - customSafeAreaInsets.top - customSafeAreaInsets.bottom
        
        let scale = UIScreen.main.scale
        metalLayer.contentsScale = scale
        // Drawable size is full bounds in pixels
        metalLayer.drawableSize = CGSize(width: bounds.size.width * scale, height: bounds.size.height * scale)

        // Start presenting after first layout when we have valid dimensions
        if !isSurfaceSet && bounds.size.width > 0 && bounds.size.height > 0 {
            AppLogger.info("[MetalView] layoutSubviews with valid bounds: \(bounds), content: \(contentWidth)x\(contentHeight) - calling startPresenting()")
            startPresenting()
        } else if isSurfaceSet {
            updateLayout()
        }
    }
    
    private func updateLayout() {
        let scale = UIScreen.main.scale
        
        // Set portrait mode before surface to ensure correct framebuffer layout
        let contentWidth = bounds.width - customSafeAreaInsets.leading - customSafeAreaInsets.trailing
        let contentHeight = bounds.height - customSafeAreaInsets.top - customSafeAreaInsets.bottom
        let portrait = contentHeight > contentWidth
        az_set_portrait_mode(portrait)
        
        az_emu_surface_set(Unmanaged.passUnretained(metalLayer).toOpaque(), Float(scale))
        az_update_framebuffer(portrait)
    }

    func startPresenting() {
        guard displayLink == nil else { 
            AppLogger.debug("[MetalView] startPresenting() called but displayLink already exists")
            return 
        }

        let contentWidth = bounds.width - customSafeAreaInsets.leading - customSafeAreaInsets.trailing
        let contentHeight = bounds.height - customSafeAreaInsets.top - customSafeAreaInsets.bottom
        AppLogger.info("[MetalView] Starting presentation - setting up Metal surface")
        AppLogger.debug("[MetalView] Bounds: \(bounds), Content: \(contentWidth)x\(contentHeight), Scale: \(UIScreen.main.scale)")
        
        // Set portrait mode BEFORE surface setup so the framebuffer layout is
        // computed correctly on the first pass. Otherwise g_is_portrait is false
        // when OnSurfaceChanged triggers UpdateCurrentFramebufferLayout, causing
        // the two 3DS screens to render side-by-side instead of stacked.
        let portrait = contentHeight > contentWidth
        az_set_portrait_mode(portrait)
        
        let scale = Float(UIScreen.main.scale)
        az_emu_surface_set(Unmanaged.passUnretained(metalLayer).toOpaque(), scale)
        isSurfaceSet = true
        
        AppLogger.info("[MetalView] Metal surface set successfully!")

        az_update_framebuffer(portrait)
        
        AppLogger.debug("[MetalView] Portrait mode: \(portrait)")

        // Add ManicEMU-style delay before starting render loop
        // This ensures Metal layer is fully initialized before rendering starts
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { [weak self] in
            guard let self = self else { return }
            
            AppLogger.info("[MetalView] Starting CADisplayLink after 1.0s delay (ManicEMU timing)")
            let link = CADisplayLink(target: self, selector: #selector(self.drawFrame))
            link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 120, preferred: 60)
            link.add(to: .main, forMode: .common)
            self.displayLink = link
            
            AppLogger.info("[MetalView] CADisplayLink started - ready to render frames")
        }
    }

    func stopPresenting() {
        displayLink?.invalidate()
        displayLink = nil
        isSurfaceSet = false
    }

    @objc private func drawFrame() {
        if az_is_running() && !az_is_paused() {
            // Try TWL (melonDS) first - if a DS game is running, blit its framebuffers
            if !az_twl_present_frame() {
                // Not in TWL mode, use normal 3DS presentation
                az_present_frame()
            }
        }
    }

    deinit {
        stopPresenting()
        az_emu_surface_destroy()
    }
}
