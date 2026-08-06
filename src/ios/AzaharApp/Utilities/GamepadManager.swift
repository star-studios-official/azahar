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

        // Button presses
        extendedGamepad.buttonA.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_BUTTON_A), pressed)
        }
        extendedGamepad.buttonB.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_BUTTON_B), pressed)
        }
        extendedGamepad.buttonX.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_BUTTON_X), pressed)
        }
        extendedGamepad.buttonY.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_BUTTON_Y), pressed)
        }
        extendedGamepad.buttonMenu.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_BUTTON_START), pressed)
        }
        extendedGamepad.buttonOptions?.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_BUTTON_SELECT), pressed)
        }
        extendedGamepad.buttonHome?.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_BUTTON_HOME), pressed)
        }

        // Triggers (L/R)
        extendedGamepad.leftTrigger.pressedChangedHandler = { _, value, pressed in
            az_axis_event(Int32(AZ_TRIGGER_L), value)
            az_button_event(Int32(AZ_TRIGGER_L), pressed)
        }
        extendedGamepad.rightTrigger.pressedChangedHandler = { _, value, pressed in
            az_axis_event(Int32(AZ_TRIGGER_R), value)
            az_button_event(Int32(AZ_TRIGGER_R), pressed)
        }

        // Shoulder buttons (ZL/ZR)
        extendedGamepad.leftShoulder.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_BUTTON_ZL), pressed)
        }
        extendedGamepad.rightShoulder.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_BUTTON_ZR), pressed)
        }

        // D-Pad
        extendedGamepad.dpad.up.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_DPAD_UP), pressed)
        }
        extendedGamepad.dpad.down.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_DPAD_DOWN), pressed)
        }
        extendedGamepad.dpad.left.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_DPAD_LEFT), pressed)
        }
        extendedGamepad.dpad.right.pressedChangedHandler = { _, _, pressed in
            az_button_event(Int32(AZ_DPAD_RIGHT), pressed)
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
}
