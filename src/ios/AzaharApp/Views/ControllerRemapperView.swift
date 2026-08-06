// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI
import GameController

/// Controller button remapper for customizing PS5/Xbox controller layouts
struct ControllerRemapperView: View {
    @Environment(\.dismiss) private var dismiss
    @StateObject private var remapper = ControllerRemapper.shared
    @State private var selectedButton: ThreeDSButton?
    @State private var isListening = false
    
    var body: some View {
        NavigationView {
            List {
                Section("3DS Buttons") {
                    buttonRow(.buttonA, "A Button", "a.circle.fill")
                    buttonRow(.buttonB, "B Button", "b.circle.fill")
                    buttonRow(.buttonX, "X Button", "x.circle.fill")
                    buttonRow(.buttonY, "Y Button", "y.circle.fill")
                    buttonRow(.buttonL, "L Trigger", "l.circle.fill")
                    buttonRow(.buttonR, "R Trigger", "r.circle.fill")
                    buttonRow(.buttonZL, "ZL Trigger", "l1.circle.fill")
                    buttonRow(.buttonZR, "ZR Trigger", "r1.circle.fill")
                }
                
                Section("D-Pad") {
                    buttonRow(.dpadUp, "D-Pad Up", "arrow.up.circle.fill")
                    buttonRow(.dpadDown, "D-Pad Down", "arrow.down.circle.fill")
                    buttonRow(.dpadLeft, "D-Pad Left", "arrow.left.circle.fill")
                    buttonRow(.dpadRight, "D-Pad Right", "arrow.right.circle.fill")
                }
                
                Section("System") {
                    buttonRow(.start, "Start", "play.circle.fill")
                    buttonRow(.select, "Select", "square.grid.2x2.fill")
                    buttonRow(.home, "Home", "house.circle.fill")
                }
                
                Section("Adaptive Triggers (PS5 DualSense)") {
                    Toggle("Enable Adaptive Triggers", isOn: $remapper.adaptiveTriggersEnabled)
                    
                    if remapper.adaptiveTriggersEnabled {
                        VStack(alignment: .leading, spacing: 8) {
                            Text("L Trigger Strength")
                                .font(.caption)
                            Slider(value: $remapper.leftTriggerStrength, in: 0...1)
                            
                            Text("R Trigger Strength")
                                .font(.caption)
                            Slider(value: $remapper.rightTriggerStrength, in: 0...1)
                        }
                    }
                }
                
                Section {
                    Button("Reset to Defaults") {
                        remapper.resetToDefaults()
                    }
                    .foregroundColor(.red)
                }
            }
            .navigationTitle("Controller Mapping")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Done") {
                        dismiss()
                    }
                }
            }
            .alert("Press Controller Button", isPresented: $isListening) {
                Button("Cancel", role: .cancel) {
                    isListening = false
                    selectedButton = nil
                }
            } message: {
                if let button = selectedButton {
                    Text("Press the controller button you want to map to \(button.displayName)")
                }
            }
        }
    }
    
    private func buttonRow(_ button: ThreeDSButton, _ title: String, _ icon: String) -> some View {
        HStack {
            Image(systemName: icon)
                .foregroundColor(.blue)
                .frame(width: 24)
            
            Text(title)
            
            Spacer()
            
            if let mapping = remapper.getMapping(for: button) {
                Text(mapping)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            
            Button("Remap") {
                selectedButton = button
                isListening = true
                remapper.startListening(for: button)
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
        }
    }
}

/// 3DS button identifiers
enum ThreeDSButton: String, CaseIterable {
    case buttonA, buttonB, buttonX, buttonY
    case buttonL, buttonR, buttonZL, buttonZR
    case dpadUp, dpadDown, dpadLeft, dpadRight
    case start, select, home
    
    var displayName: String {
        switch self {
        case .buttonA: return "A"
        case .buttonB: return "B"
        case .buttonX: return "X"
        case .buttonY: return "Y"
        case .buttonL: return "L"
        case .buttonR: return "R"
        case .buttonZL: return "ZL"
        case .buttonZR: return "ZR"
        case .dpadUp: return "D-Pad Up"
        case .dpadDown: return "D-Pad Down"
        case .dpadLeft: return "D-Pad Left"
        case .dpadRight: return "D-Pad Right"
        case .start: return "Start"
        case .select: return "Select"
        case .home: return "Home"
        }
    }
    
    var azButtonCode: Int32 {
        switch self {
        case .buttonA: return Int32(AZ_BUTTON_A)
        case .buttonB: return Int32(AZ_BUTTON_B)
        case .buttonX: return Int32(AZ_BUTTON_X)
        case .buttonY: return Int32(AZ_BUTTON_Y)
        case .buttonL: return Int32(AZ_TRIGGER_L)
        case .buttonR: return Int32(AZ_TRIGGER_R)
        case .buttonZL: return Int32(AZ_BUTTON_ZL)
        case .buttonZR: return Int32(AZ_BUTTON_ZR)
        case .dpadUp: return Int32(AZ_DPAD_UP)
        case .dpadDown: return Int32(AZ_DPAD_DOWN)
        case .dpadLeft: return Int32(AZ_DPAD_LEFT)
        case .dpadRight: return Int32(AZ_DPAD_RIGHT)
        case .start: return Int32(AZ_BUTTON_START)
        case .select: return Int32(AZ_BUTTON_SELECT)
        case .home: return Int32(AZ_BUTTON_HOME)
        }
    }
}

/// Controller remapper singleton managing button mappings
class ControllerRemapper: ObservableObject {
    static let shared = ControllerRemapper()
    
    @Published var adaptiveTriggersEnabled = true
    @Published var leftTriggerStrength: Double = 0.5
    @Published var rightTriggerStrength: Double = 0.5
    
    private var mappings: [ThreeDSButton: String] = [:]
    private var listeningFor: ThreeDSButton?
    
    private init() {
        loadMappings()
    }
    
    func getMapping(for button: ThreeDSButton) -> String? {
        return mappings[button]
    }
    
    func startListening(for button: ThreeDSButton) {
        listeningFor = button
        // GamepadManager will call setMapping when a button is pressed
    }
    
    func setMapping(for button: ThreeDSButton, controllerButton: String) {
        mappings[button] = controllerButton
        saveMappings()
        listeningFor = nil
    }
    
    func resetToDefaults() {
        mappings.removeAll()
        adaptiveTriggersEnabled = true
        leftTriggerStrength = 0.5
        rightTriggerStrength = 0.5
        saveMappings()
    }
    
    private func loadMappings() {
        if let data = UserDefaults.standard.data(forKey: "ControllerMappings"),
           let decoded = try? JSONDecoder().decode([String: String].self, from: data) {
            for (key, value) in decoded {
                if let button = ThreeDSButton(rawValue: key) {
                    mappings[button] = value
                }
            }
        }
        
        adaptiveTriggersEnabled = UserDefaults.standard.object(forKey: "AdaptiveTriggersEnabled") as? Bool ?? true
        leftTriggerStrength = UserDefaults.standard.object(forKey: "LeftTriggerStrength") as? Double ?? 0.5
        rightTriggerStrength = UserDefaults.standard.object(forKey: "RightTriggerStrength") as? Double ?? 0.5
    }
    
    private func saveMappings() {
        let dict = mappings.reduce(into: [String: String]()) { result, pair in
            result[pair.key.rawValue] = pair.value
        }
        if let data = try? JSONEncoder().encode(dict) {
            UserDefaults.standard.set(data, forKey: "ControllerMappings")
        }
        
        UserDefaults.standard.set(adaptiveTriggersEnabled, forKey: "AdaptiveTriggersEnabled")
        UserDefaults.standard.set(leftTriggerStrength, forKey: "LeftTriggerStrength")
        UserDefaults.standard.set(rightTriggerStrength, forKey: "RightTriggerStrength")
    }
}
