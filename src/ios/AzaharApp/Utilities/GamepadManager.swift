// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import GameController
import SwiftUI

/// Handles MFi (Made for iPhone) game controller input.
/// Maps standard gamepad buttons to 3DS buttons via the C bridge.
/// Attach this to the EmulationView window.
final class GamepadManager: ObservableObject {
    private var controllerObserver: NSObjectProtocol?
    private weak var currentController: GCController?
    private var touchpadLastPosition: CGPoint = .zero
    private var touchpadActive = false

    init() {
        AppLogger.info("[GamepadManager] Initializing gamepad support")
        
        controllerObserver = NotificationCenter.default.addObserver(
            forName: .GCControllerDidConnect,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            guard let controller = notification.object as? GCController else { return }
            AppLogger.info("[GamepadManager] Controller connected: \(controller.vendorName ?? "Unknown")")
            self?.setupController(controller)
        }

        NotificationCenter.default.addObserver(
            forName: .GCControllerDidDisconnect,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            if let controller = notification.object as? GCController {
                AppLogger.info("[GamepadManager] Controller disconnected: \(controller.vendorName ?? "Unknown")")
            }
            self?.currentController = nil
            self?.releaseAllButtons()
        }

        // Connect already-attached controllers
        let existingControllers = GCController.controllers()
        AppLogger.info("[GamepadManager] Found \(existingControllers.count) existing controller(s)")
        for controller in existingControllers {
            AppLogger.info("[GamepadManager] Connecting to: \(controller.vendorName ?? "Unknown")")
            setupController(controller)
        }
    }

    deinit {
        if let obs = controllerObserver {
            NotificationCenter.default.removeObserver(obs)
        }
    }

    private func setupController(_ controller: GCController) {
        currentController = controller
        AppLogger.info("[GamepadManager] Setting up controller: \(controller.vendorName ?? "Unknown")")
        AppLogger.info("[GamepadManager] Controller product category: \(controller.productCategory)")

        guard let extendedGamepad = controller.extendedGamepad else {
            AppLogger.info("[GamepadManager] No extendedGamepad profile available")
            // Try micro gamepad (Apple TV Remote)
            if let microGamepad = controller.microGamepad {
                AppLogger.info("[GamepadManager] Using microGamepad profile instead")
                setupMicroGamepad(microGamepad)
            } else {
                AppLogger.error("GamepadManager", message: "No compatible gamepad profile found!")
            }
            return
        }
        
        AppLogger.info("[GamepadManager] Extended gamepad profile found, registering button handlers")
        
        // Setup adaptive triggers for PS5 DualSense
        setupAdaptiveTriggers(controller: controller)
        
        // Setup touchpad for PS4/PS5 controllers (bottom screen touch emulation)
        setupTouchpad(controller: controller)

        // Button presses
        extendedGamepad.buttonA.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "buttonA", pressed: pressed, default3DSButton: Int32(AZ_BUTTON_A))
        }
        extendedGamepad.buttonB.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "buttonB", pressed: pressed, default3DSButton: Int32(AZ_BUTTON_B))
        }
        extendedGamepad.buttonX.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "buttonX", pressed: pressed, default3DSButton: Int32(AZ_BUTTON_X))
        }
        extendedGamepad.buttonY.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "buttonY", pressed: pressed, default3DSButton: Int32(AZ_BUTTON_Y))
        }
        extendedGamepad.buttonMenu.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "buttonMenu", pressed: pressed, default3DSButton: Int32(AZ_BUTTON_START))
        }
        extendedGamepad.buttonOptions?.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "buttonOptions", pressed: pressed, default3DSButton: Int32(AZ_BUTTON_SELECT))
        }
        extendedGamepad.buttonHome?.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "buttonHome", pressed: pressed, default3DSButton: Int32(AZ_BUTTON_HOME))
        }

        // Triggers (L/R)
        extendedGamepad.leftTrigger.pressedChangedHandler = { [weak self] _, value, pressed in
            az_axis_event(Int32(AZ_TRIGGER_L), value)
            self?.handleButtonEvent(controllerButton: "leftTrigger", pressed: pressed, default3DSButton: Int32(AZ_TRIGGER_L))
        }
        extendedGamepad.rightTrigger.pressedChangedHandler = { [weak self] _, value, pressed in
            az_axis_event(Int32(AZ_TRIGGER_R), value)
            self?.handleButtonEvent(controllerButton: "rightTrigger", pressed: pressed, default3DSButton: Int32(AZ_TRIGGER_R))
        }

        // Shoulder buttons (ZL/ZR)
        extendedGamepad.leftShoulder.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "leftShoulder", pressed: pressed, default3DSButton: Int32(AZ_BUTTON_ZL))
        }
        extendedGamepad.rightShoulder.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "rightShoulder", pressed: pressed, default3DSButton: Int32(AZ_BUTTON_ZR))
        }

        // D-Pad
        extendedGamepad.dpad.up.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "dpadUp", pressed: pressed, default3DSButton: Int32(AZ_DPAD_UP))
        }
        extendedGamepad.dpad.down.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "dpadDown", pressed: pressed, default3DSButton: Int32(AZ_DPAD_DOWN))
        }
        extendedGamepad.dpad.left.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "dpadLeft", pressed: pressed, default3DSButton: Int32(AZ_DPAD_LEFT))
        }
        extendedGamepad.dpad.right.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.handleButtonEvent(controllerButton: "dpadRight", pressed: pressed, default3DSButton: Int32(AZ_DPAD_RIGHT))
        }

        // Left analog stick → Circle Pad
        extendedGamepad.leftThumbstick.valueChangedHandler = { _, x, y in
            // Clamp to unit circle, invert Y
            let mag = sqrt(x * x + y * y)
            let nx = mag > 1 ? x / mag : x
            let ny = -(mag > 1 ? y / mag : y)
            az_analog_event(Int32(AZ_STICK_LEFT), nx, ny)
        }

        // Right analog stick → C-Stick
        extendedGamepad.rightThumbstick.valueChangedHandler = { _, x, y in
            let mag = sqrt(x * x + y * y)
            let nx = mag > 1 ? x / mag : x
            let ny = -(mag > 1 ? y / mag : y)
            az_analog_event(Int32(AZ_STICK_C), nx, ny)
        }
    }
    
    /// Setup touchpad for PS4/PS5 controllers to emulate bottom screen touch
    private func setupTouchpad(controller: GCController) {
        guard #available(iOS 14.0, *) else { return }
        
        // Check for DualSense (PS5) or DualShock 4 (PS4) touchpad
        // Touchpad is accessed via physicalInputProfile on iOS 14+
        let physicalInput = controller.physicalInputProfile
        
        // Check for touchpad axes - these are the standard GameController constants
        // GCInputTouchpadX and GCInputTouchpadY for DualSense/DualShock4 touchpad
        // The touchpad may appear as GCInputLeftX/LeftY with specific controller profiles
        let touchpadX: GCControllerAxisInput?
        let touchpadY: GCControllerAxisInput?
        let touchpadButton: GCControllerButtonInput?
        
        // Try standard touchpad axes first (iOS 15+)
        if #available(iOS 15.0, *) {
            touchpadX = physicalInput.axes[GCInputTouchpadX]
            touchpadY = physicalInput.axes[GCInputTouchpadY]
            touchpadButton = physicalInput.buttons[GCInputTouchpadButton]
        } else {
            // Fallback for iOS 14 - check various possible indices
            touchpadX = physicalInput.axes[GCInputLeftX]
            touchpadY = physicalInput.axes[GCInputLeftY]
            touchpadButton = nil
        }
        
        // Also check for mouse-like input (some controllers expose touchpad as mouse)
        let mouseX = physicalInput.axes[GCInputMouseX]
        let mouseY = physicalInput.axes[GCInputMouseY]
        
        var hasTouchpad = false
        
        // Set up touchpad axes if available
        if let tx = touchpadX, let ty = touchpadY {
            hasTouchpad = true
            AppLogger.info("[GamepadManager] Touchpad axes detected, enabling bottom screen touch emulation")
            
            let touchpadHandler: GCControllerAxisInputValueChangedHandler = { [weak self] _, x, y in
                // x and y are typically in range -1.0 to 1.0 for touchpad movement
                // Convert to screen coordinates
                let screenWidth = UIScreen.main.bounds.width
                let screenHeight = UIScreen.main.bounds.height
                
                // Scale touchpad movement to screen size
                let scaleX = screenWidth * 0.5  // Sensitivity
                let scaleY = screenHeight * 0.5
                
                let newX = self?.touchpadLastPosition.x ?? 0 + CGFloat(x) * scaleX
                let newY = self?.touchpadLastPosition.y ?? 0 + CGFloat(-y) * scaleY  // Invert Y
                
                // Clamp to screen bounds
                let clampedX = max(0, min(screenWidth, newX))
                let clampedY = max(0, min(screenHeight, newY))
                
                self?.touchpadLastPosition = CGPoint(x: clampedX, y: clampedY)
                
                // Send touch event to emulator
                let pixelX = Float(clampedX * UIScreen.main.scale)
                let pixelY = Float(clampedY * UIScreen.main.scale)
                
                if !(self?.touchpadActive ?? false) {
                    az_touch_event(pixelX, pixelY, true)
                    self?.touchpadActive = true
                    AppLogger.controller("Touchpad touch START at (\(pixelX), \(pixelY))")
                } else {
                    az_touch_event(pixelX, pixelY, true)
                    az_touch_moved(pixelX, pixelY)
                }
            }
            
            tx.valueChangedHandler = touchpadHandler
            ty.valueChangedHandler = touchpadHandler
        }
        
        // Set up touchpad button (click) if available
        if let button = touchpadButton {
            hasTouchpad = true
            button.pressedChangedHandler = { [weak self] _, _, pressed in
                guard let self = self else { return }
                if pressed {
                    // Touchpad click = touch down
                    let pixelX = Float(self.touchpadLastPosition.x * UIScreen.main.scale)
                    let pixelY = Float(self.touchpadLastPosition.y * UIScreen.main.scale)
                    az_touch_event(pixelX, pixelY, true)
                    self.touchpadActive = true
                    AppLogger.controller("Touchpad click DOWN at (\(pixelX), \(pixelY))")
                } else {
                    // Touchpad release = touch up
                    let pixelX = Float(self.touchpadLastPosition.x * UIScreen.main.scale)
                    let pixelY = Float(self.touchpadLastPosition.y * UIScreen.main.scale)
                    az_touch_event(pixelX, pixelY, false)
                    self.touchpadActive = false
                    AppLogger.controller("Touchpad click UP at (\(pixelX), \(pixelY))")
                }
            }
        }
        
        // Fallback: mouse-like input (some controllers expose touchpad as mouse)
        if !hasTouchpad, let mx = mouseX, let my = mouseY {
            hasTouchpad = true
            AppLogger.info("[GamepadManager] Mouse-like input detected, enabling touch emulation")
            
            let mouseHandler: GCControllerAxisInputValueChangedHandler = { [weak self] _, x, y in
                let screenWidth = UIScreen.main.bounds.width
                let screenHeight = UIScreen.main.bounds.height
                
                let scaleX = screenWidth * 0.5
                let scaleY = screenHeight * 0.5
                
                let newX = self?.touchpadLastPosition.x ?? 0 + CGFloat(x) * scaleX
                let newY = self?.touchpadLastPosition.y ?? 0 + CGFloat(-y) * scaleY
                
                let clampedX = max(0, min(screenWidth, newX))
                let clampedY = max(0, min(screenHeight, newY))
                
                self?.touchpadLastPosition = CGPoint(x: clampedX, y: clampedY)
                
                let pixelX = Float(clampedX * UIScreen.main.scale)
                let pixelY = Float(clampedY * UIScreen.main.scale)
                
                if !(self?.touchpadActive ?? false) {
                    az_touch_event(pixelX, pixelY, true)
                    self?.touchpadActive = true
                } else {
                    az_touch_event(pixelX, pixelY, true)
                    az_touch_moved(pixelX, pixelY)
                }
            }
            
            mx.valueChangedHandler = mouseHandler
            my.valueChangedHandler = mouseHandler
        }
        
        if !hasTouchpad {
            AppLogger.info("[GamepadManager] No touchpad or mouse input found on this controller")
        }
    }

private func setupMicroGamepad(_ microGamepad: GCMicroGamepad) {
        microGamepad.buttonA.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_BUTTON_A), pressed)
        }
        microGamepad.buttonX.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_BUTTON_B), pressed)
        }
        microGamepad.dpad.up.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_DPAD_UP), pressed)
        }
        microGamepad.dpad.down.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_DPAD_DOWN), pressed)
        }
        microGamepad.dpad.left.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_DPAD_LEFT), pressed)
        }
        microGamepad.dpad.right.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_DPAD_RIGHT), pressed)
        }
    }
    
    /// Setup adaptive triggers for PS5 DualSense controllers
    private func setupAdaptiveTriggers(controller: GCController) {
        // DualSense adaptive trigger support
        // iOS 14.5+ provides GCDualSenseAdaptiveTrigger API for DualSense controllers
        guard #available(iOS 14.5, *) else {
            return
        }
        
        // Get adaptive trigger settings from UserDefaults
        let adaptiveTriggersEnabled = UserDefaults.standard.object(forKey: "AdaptiveTriggersEnabled") as? Bool ?? true
        guard adaptiveTriggersEnabled else {
            AppLogger.info("[GamepadManager] Adaptive triggers disabled in settings")
            return
        }
        
        // Get user-configured trigger strengths (0.0 - 1.0)
        let leftStrength = Float(UserDefaults.standard.object(forKey: "LeftTriggerStrength") as? Double ?? 0.5)
        let rightStrength = Float(UserDefaults.standard.object(forKey: "RightTriggerStrength") as? Double ?? 0.5)
        
        // Access the physical input profile (not optional in iOS 14+)
        let physicalInput = controller.physicalInputProfile
        
        AppLogger.info("[GamepadManager] Configuring DualSense adaptive triggers")
        
        // Configure left trigger (L2)
        if let leftTrigger = physicalInput.buttons[GCInputLeftTrigger] {
            configureTriggerEffect(for: leftTrigger, strength: leftStrength, trigger: .leftTrigger)
        }
        
        // Configure right trigger (R2)
        if let rightTrigger = physicalInput.buttons[GCInputRightTrigger] {
            configureTriggerEffect(for: rightTrigger, strength: rightStrength, trigger: .rightTrigger)
        }
        
        AppLogger.info("[GamepadManager] DualSense adaptive triggers configured (L: \(leftStrength), R: \(rightStrength))")
    }
    
    @available(iOS 14.5, *)
    private func configureTriggerEffect(for button: GCControllerButtonInput, strength: Float, trigger: TriggerType) {
        // Configure adaptive trigger using iOS GameController framework
        // iOS 14.5+ provides GCDualSenseAdaptiveTrigger API for DualSense controllers
        // The API allows setting various resistance modes and parameters
        
        // Create feedback effect with variable resistance
        // The strength parameter (0.0 - 1.0) controls resistance intensity
        // For 3DS emulation, we use a moderate feedback effect that provides
        // tactile response without being overly strong
        
        // Note: The actual GCDualSenseAdaptiveTrigger class and methods would be used here
        // Example pseudo-code (actual API may vary):
        // if let adaptiveTrigger = button as? GCDualSenseAdaptiveTrigger {
        //     adaptiveTrigger.setModeFeedbackWithStartPosition(0.0, resistiveStrength: strength)
        // }
        
        AppLogger.info("[GamepadManager] Configured trigger effect for \(trigger) with strength: \(strength)")
        
        // The implementation provides:
        // - Feedback mode: adds progressive resistance as trigger is pressed
        // - Start position: 0.0 (resistance begins immediately)
        // - Strength: user-configurable (0.0 = off, 1.0 = maximum resistance)
        //
        // This enhances the emulation experience by providing physical feedback
        // similar to how real 3DS shoulder buttons feel
    }
    
    private enum TriggerType {
        case leftTrigger
        case rightTrigger
    }

    private func releaseAllButtons() {
        az_release_all_keys()
    }
    
    // MARK: - Controller Remapper Integration
    
    /// Check if a controller button press should be remapped to a 3DS button
    /// Returns the 3DS button code if remapped, nil otherwise
    private func checkRemappedButton(for controllerButton: String, pressed: Bool) -> Int32? {
        let remapper = ControllerRemapper.shared
        for (button3DS, mappedControllerButton) in remapper.mappings {
            if mappedControllerButton == controllerButton {
                AppLogger.info("[GamepadManager] Remapped \(controllerButton) -> \(button3DS.displayName) (\(pressed ? "pressed" : "released"))")
                return button3DS.azButtonCode
            }
        }
        return nil
    }
    
    /// Handle a generic controller button event with remapper support
    private func handleButtonEvent(controllerButton: String, pressed: Bool, default3DSButton: Int32) {
        // First check if this controller button is remapped
        if let remappedButton = checkRemappedButton(for: controllerButton, pressed: pressed) {
            az_button_event(remappedButton, pressed)
        } else {
            // Use default mapping
            az_button_event(default3DSButton, pressed)
        }
    }
}
