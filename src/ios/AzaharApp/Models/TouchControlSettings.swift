// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import Foundation
import UIKit

/// Touch control layout settings (matches Android InputOverlay logic)
class TouchControlSettings: ObservableObject, Codable {
    // Button scales (0.0 - 2.0, default based on Android)
    @Published var faceButtonScale: CGFloat = 0.5      // A/B/X/Y (50% of screen min dimension)
    @Published var dpadScale: CGFloat = 0.5            // D-Pad
    @Published var triggerScale: CGFloat = 0.7         // L/R/ZL/ZR (70%)
    @Published var joystickScale: CGFloat = 0.7        // Circle Pad / C-Stick (70%)
    @Published var centerButtonScale: CGFloat = 0.4    // Start/Select/Home
    
    // Button positions (0.0 - 1.0 as fraction of screen)
    // Landscape layout
    @Published var landscapePositions: [String: CGPoint] = [:]
    
    // Portrait layout
    @Published var portraitPositions: [String: CGPoint] = [:]
    
    // Button opacity (0.0 - 1.0)
    @Published var buttonOpacity: CGFloat = 0.7
    
    // Edit mode enabled
    @Published var isEditModeEnabled: Bool = false
    
    static let shared = TouchControlSettings()
    
    private static let userDefaultsKey = "TouchControlSettings"
    
    private enum CodingKeys: String, CodingKey {
        case faceButtonScale, dpadScale, triggerScale, joystickScale, centerButtonScale
        case landscapePositions, portraitPositions
        case buttonOpacity, isEditModeEnabled
    }
    
    required init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let _faceButtonScale = try container.decode(CGFloat.self, forKey: .faceButtonScale)
        let _dpadScale = try container.decode(CGFloat.self, forKey: .dpadScale)
        let _triggerScale = try container.decode(CGFloat.self, forKey: .triggerScale)
        let _joystickScale = try container.decode(CGFloat.self, forKey: .joystickScale)
        let _centerButtonScale = try container.decode(CGFloat.self, forKey: .centerButtonScale)
        let _landscapePositions = try container.decode([String: CGPoint].self, forKey: .landscapePositions)
        let _portraitPositions = try container.decode([String: CGPoint].self, forKey: .portraitPositions)
        let _buttonOpacity = try container.decode(CGFloat.self, forKey: .buttonOpacity)
        let _isEditModeEnabled = try container.decode(Bool.self, forKey: .isEditModeEnabled)
        
        faceButtonScale = _faceButtonScale
        dpadScale = _dpadScale
        triggerScale = _triggerScale
        joystickScale = _joystickScale
        centerButtonScale = _centerButtonScale
        landscapePositions = _landscapePositions
        portraitPositions = _portraitPositions
        buttonOpacity = _buttonOpacity
        isEditModeEnabled = _isEditModeEnabled
    }
    
    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(faceButtonScale, forKey: .faceButtonScale)
        try container.encode(dpadScale, forKey: .dpadScale)
        try container.encode(triggerScale, forKey: .triggerScale)
        try container.encode(joystickScale, forKey: .joystickScale)
        try container.encode(centerButtonScale, forKey: .centerButtonScale)
        try container.encode(landscapePositions, forKey: .landscapePositions)
        try container.encode(portraitPositions, forKey: .portraitPositions)
        try container.encode(buttonOpacity, forKey: .buttonOpacity)
        try container.encode(isEditModeEnabled, forKey: .isEditModeEnabled)
    }
    
    init() {
        // Default initializer for creating new instances
    }
    
    static func load() -> TouchControlSettings {
        guard let data = UserDefaults.standard.data(forKey: userDefaultsKey),
              let settings = try? JSONDecoder().decode(TouchControlSettings.self, from: data) else {
            return TouchControlSettings()
        }
        return settings
    }
    
    func save() {
        if let data = try? JSONEncoder().encode(self) {
            UserDefaults.standard.set(data, forKey: Self.userDefaultsKey)
        }
    }
    
    func resetToDefaults() {
        let settings = TouchControlSettings()
        settings.save()
    }
    
    /// Calculate button size based on Android algorithm
    /// Android: min(screenWidth, screenHeight) * scale
    func buttonSize(for type: ButtonType, screenSize: CGSize) -> CGFloat {
        let minDimension = min(screenSize.width, screenSize.height)
        
        let scale: CGFloat
        
        switch type {
        case .faceButton:
            scale = faceButtonScale
        case .dpad:
            scale = dpadScale
        case .trigger:
            scale = triggerScale
        case .joystick:
            scale = joystickScale
        case .centerButton:
            scale = centerButtonScale
        }
        
        // Portrait gets 0.30x scale, landscape gets 0.20x scale (ManicEmu-style smaller buttons)
        // This prevents buttons from being enormous in landscape mode
        let isPortrait = screenSize.height > screenSize.width
        let sizeAdjustment: CGFloat = isPortrait ? 0.30 : 0.20
        
        return minDimension * scale * sizeAdjustment
    }
    
    enum ButtonType {
        case faceButton  // A, B, X, Y
        case dpad
        case trigger     // L, R, ZL, ZR
        case joystick    // Circle Pad, C-Stick
        case centerButton // Start, Select, Home
    }
}
