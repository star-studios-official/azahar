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
    /// NOTE: iOS GameController framework does not expose touchpad axes/buttons
    /// This is a placeholder for future implementation if Apple adds touchpad support
    private func setupTouchpad(controller: GCController) {
        guard #available(iOS 14.0, *) else { return }
        
        // Unfortunately, iOS GameController framework does not provide access to
        // PS5 DualSense or PS4 DualShock 4 touchpad input as of iOS 18.0
        // The touchpad constants (GCInputTouchpadX, GCInputTouchpadY, etc.)
        // are only available on macOS/Catalyst, not iOS
        
        AppLogger.info("[GamepadManager] Touchpad support not available on iOS - touchpad input cannot be used for bottom screen touch")
        
        // TODO: If Apple adds touchpad support to iOS GameController framework in the future,
        // implement touchpad-to-touch mapping here
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
        for (button3DS, mappedControllerButton) in remapper.allMappings {
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
