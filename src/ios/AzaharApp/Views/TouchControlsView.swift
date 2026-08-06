// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI

/// On-screen touch controls overlay using PNG assets from Android
/// Positions match the Android InputOverlay layout
struct TouchControlsView: View {
    @ObservedObject var viewModel: EmulationViewModel
    @StateObject private var controllerManager = ControllerManager.shared
    @ObservedObject private var settings = TouchControlSettings.shared
    @Environment(\.horizontalSizeClass) private var sizeClass
    @State private var draggedButton: String?
    @State private var dragOffset: CGSize = .zero

    var body: some View {
        GeometryReader { geometry in
            let w = geometry.size.width
            let h = geometry.size.height
            let isLandscape = w > h
            
            // Calculate button sizes based on Android algorithm
            let faceButtonSize = settings.buttonSize(for: .faceButton, screenSize: geometry.size)
            let dpadSize = settings.buttonSize(for: .dpad, screenSize: geometry.size)
            let triggerSize = settings.buttonSize(for: .trigger, screenSize: geometry.size)
            let joystickSize = settings.buttonSize(for: .joystick, screenSize: geometry.size)
            let centerButtonSize = settings.buttonSize(for: .centerButton, screenSize: geometry.size)
            
            ZStack {
                // Hide controls when controller is connected (unless in edit mode)
                if !controllerManager.isControllerConnected || settings.isEditModeEnabled {
                    if isLandscape {
                        landscapeControls(
                            width: w,
                            height: h,
                            faceButtonSize: faceButtonSize,
                            dpadSize: dpadSize,
                            triggerSize: triggerSize,
                            joystickSize: joystickSize,
                            centerButtonSize: centerButtonSize
                        )
                    } else {
                        portraitControls(
                            width: w,
                            height: h,
                            faceButtonSize: faceButtonSize,
                            dpadSize: dpadSize,
                            triggerSize: triggerSize,
                            joystickSize: joystickSize,
                            centerButtonSize: centerButtonSize
                        )
                    }
                }
                
                // Always show pause button (even with controller)
                VStack {
                    HStack {
                        Spacer()
                        Button {
                            viewModel.togglePause()
                        } label: {
                            Image(systemName: "pause.circle.fill")
                                .font(.system(size: 44))
                                .foregroundStyle(.white.opacity(0.8))
                                .shadow(radius: 2)
                        }
                        .padding(16)
                    }
                    Spacer()
                }
                
                // Edit mode overlay
                if settings.isEditModeEnabled {
                    editModeOverlay(geometry: geometry)
                }
            }
        }
        .allowsHitTesting(viewModel.isControlsVisible || settings.isEditModeEnabled)
    }
    
    private func landscapeControls(
        width: CGFloat,
        height: CGFloat,
        faceButtonSize: CGFloat,
        dpadSize: CGFloat,
        triggerSize: CGFloat,
        joystickSize: CGFloat,
        centerButtonSize: CGFloat
    ) -> some View {
        ZStack {
            // D-Pad (left side) - Position: (15, 470) out of 1000
            DPadView(size: dpadSize)
                .position(x: width * 0.015 + dpadSize/2, y: height * 0.470 + dpadSize/2)
            
            // Left Analog Stick - Position: (100, 670) out of 1000
            AnalogStickView(
                position: $viewModel.leftStickPosition,
                onPositionChanged: { x, y in
                    // Normalize by maxRadius to get values in [-1.0, 1.0]
                    let maxRadius = joystickSize * 0.35
                    let nx = Float(x / maxRadius)
                    let ny = Float(y / maxRadius)
                    az_analog_event(Int32(AZ_STICK_LEFT), nx, ny)
                },
                size: joystickSize,
                isCirclePad: true
            )
            .position(x: width * 0.100, y: height * 0.670)
            
            // Right Analog Stick (C-Stick) - Position: (740, 770) out of 1000
            AnalogStickView(
                position: $viewModel.rightStickPosition,
                onPositionChanged: { x, y in
                    // Normalize by maxRadius to get values in [-1.0, 1.0]
                    let maxRadius = joystickSize * 0.35
                    let nx = Float(x / maxRadius)
                    let ny = Float(y / maxRadius)
                    az_analog_event(Int32(AZ_STICK_C), nx, ny)
                },
                size: joystickSize,
                isCirclePad: false
            )
            .position(x: width * 0.740, y: height * 0.770)
            
            // Face buttons (A/B/X/Y) - Right side
            // Button A - Position: (930, 620)
            ButtonImage(name: "button_a", button: Int32(AZ_BUTTON_A), size: faceButtonSize, opacity: settings.buttonOpacity)
                .position(x: width * 0.930, y: height * 0.620)
            
            // Button B - Position: (870, 720)
            ButtonImage(name: "button_b", button: Int32(AZ_BUTTON_B), size: faceButtonSize, opacity: settings.buttonOpacity)
                .position(x: width * 0.870, y: height * 0.720)
            
            // Button X - Position: (870, 520)
            ButtonImage(name: "button_x", button: Int32(AZ_BUTTON_X), size: faceButtonSize, opacity: settings.buttonOpacity)
                .position(x: width * 0.870, y: height * 0.520)
            
            // Button Y - Position: (810, 620)
            ButtonImage(name: "button_y", button: Int32(AZ_BUTTON_Y), size: faceButtonSize, opacity: settings.buttonOpacity)
                .position(x: width * 0.810, y: height * 0.620)
            
            // L Trigger - Position: (13, 0)
            ButtonImage(name: "button_l", button: Int32(AZ_TRIGGER_L), size: triggerSize, opacity: settings.buttonOpacity)
                .position(x: width * 0.013 + triggerSize/2, y: height * 0.05)
            
            // R Trigger - Position: (895, 0)
            ButtonImage(name: "button_r", button: Int32(AZ_TRIGGER_R), size: triggerSize, opacity: settings.buttonOpacity)
                .position(x: width * 0.895 + triggerSize/2, y: height * 0.05)
            
            // ZL Trigger - Position: (13, 110)
            ButtonImage(name: "button_zl", button: Int32(AZ_BUTTON_ZL), size: triggerSize, opacity: settings.buttonOpacity)
                .position(x: width * 0.013 + triggerSize/2, y: height * 0.110 + triggerSize/2)
            
            // ZR Trigger - Position: (895, 110)
            ButtonImage(name: "button_zr", button: Int32(AZ_BUTTON_ZR), size: triggerSize, opacity: settings.buttonOpacity)
                .position(x: width * 0.895 + triggerSize/2, y: height * 0.110 + triggerSize/2)
            
            // Center buttons
            HStack(spacing: 12) {
                // Select - Position: (470, 850)
                ButtonImage(name: "button_select", button: Int32(AZ_BUTTON_SELECT), size: centerButtonSize, opacity: settings.buttonOpacity)
                
                // Start - Position: (550, 850)
                ButtonImage(name: "button_start", button: Int32(AZ_BUTTON_START), size: centerButtonSize, opacity: settings.buttonOpacity)
            }
            .position(x: width * 0.510, y: height * 0.850)
        }
    }
    
    private func portraitControls(
        width: CGFloat,
        height: CGFloat,
        faceButtonSize: CGFloat,
        dpadSize: CGFloat,
        triggerSize: CGFloat,
        joystickSize: CGFloat,
        centerButtonSize: CGFloat
    ) -> some View {
        ZStack {
            // D-Pad (left side) - Lower left area
            DPadView(size: dpadSize)
                .position(x: dpadSize/2 + 20, y: height - dpadSize/2 - 140)
            
            // Left Analog Stick - Lower left
            AnalogStickView(
                position: $viewModel.leftStickPosition,
                onPositionChanged: { x, y in
                    // Normalize by maxRadius to get values in [-1.0, 1.0]
                    let maxRadius = joystickSize * 0.35
                    let nx = Float(x / maxRadius)
                    let ny = Float(y / maxRadius)
                    az_analog_event(Int32(AZ_STICK_LEFT), nx, ny)
                },
                size: joystickSize,
                isCirclePad: true
            )
            .position(x: joystickSize/2 + 20, y: height - joystickSize/2 - 20)
            
            // Right Analog Stick (C-Stick) - Lower right area, above face buttons
            AnalogStickView(
                position: $viewModel.rightStickPosition,
                onPositionChanged: { x, y in
                    // Normalize by maxRadius to get values in [-1.0, 1.0]
                    let maxRadius = joystickSize * 0.35
                    let nx = Float(x / maxRadius)
                    let ny = Float(y / maxRadius)
                    az_analog_event(Int32(AZ_STICK_C), nx, ny)
                },
                size: joystickSize,
                isCirclePad: false
            )
            .position(x: width - joystickSize/2 - 20, y: height - joystickSize/2 - 180)
            
            // Face buttons - Lower right in diamond pattern
            // Center of diamond
            let faceButtonCenterX = width - 100
            let faceButtonCenterY = height - 80
            let buttonSpacing: CGFloat = 50
            
            // Button A - Right
            ButtonImage(name: "button_a", button: Int32(AZ_BUTTON_A), size: faceButtonSize, opacity: settings.buttonOpacity)
                .position(x: faceButtonCenterX + buttonSpacing, y: faceButtonCenterY)
            
            // Button B - Bottom
            ButtonImage(name: "button_b", button: Int32(AZ_BUTTON_B), size: faceButtonSize, opacity: settings.buttonOpacity)
                .position(x: faceButtonCenterX, y: faceButtonCenterY + buttonSpacing)
            
            // Button X - Top
            ButtonImage(name: "button_x", button: Int32(AZ_BUTTON_X), size: faceButtonSize, opacity: settings.buttonOpacity)
                .position(x: faceButtonCenterX, y: faceButtonCenterY - buttonSpacing)
            
            // Button Y - Left
            ButtonImage(name: "button_y", button: Int32(AZ_BUTTON_Y), size: faceButtonSize, opacity: settings.buttonOpacity)
                .position(x: faceButtonCenterX - buttonSpacing, y: faceButtonCenterY)
            
            // Shoulder buttons - Top edge in a row
            HStack(spacing: 12) {
                ButtonImage(name: "button_l", button: Int32(AZ_TRIGGER_L), size: triggerSize * 0.8, opacity: settings.buttonOpacity)
                ButtonImage(name: "button_zl", button: Int32(AZ_BUTTON_ZL), size: triggerSize * 0.8, opacity: settings.buttonOpacity)
                Spacer()
                ButtonImage(name: "button_zr", button: Int32(AZ_BUTTON_ZR), size: triggerSize * 0.8, opacity: settings.buttonOpacity)
                ButtonImage(name: "button_r", button: Int32(AZ_TRIGGER_R), size: triggerSize * 0.8, opacity: settings.buttonOpacity)
            }
            .position(x: width/2, y: height - height * 0.36)
            
            // Center buttons - Middle area between controls
            HStack(spacing: 20) {
                ButtonImage(name: "button_select", button: Int32(AZ_BUTTON_SELECT), size: centerButtonSize, opacity: settings.buttonOpacity)
                ButtonImage(name: "button_start", button: Int32(AZ_BUTTON_START), size: centerButtonSize, opacity: settings.buttonOpacity)
            }
            .position(x: width/2, y: height - height * 0.25)
        }
    }
    
    // Edit mode overlay for dragging/resizing buttons
    private func editModeOverlay(geometry: GeometryProxy) -> some View {
        VStack {
            HStack {
                Text("Edit Controls")
                    .font(.headline)
                    .foregroundStyle(.white)
                    .padding(8)
                    .background(.blue.opacity(0.8), in: RoundedRectangle(cornerRadius: 8))
                
                Spacer()
                
                Button("Reset") {
                    settings.resetToDefaults()
                }
                .buttonStyle(.bordered)
                .tint(.red)
                
                Button("Done") {
                    settings.isEditModeEnabled = false
                    settings.save()
                }
                .buttonStyle(.borderedProminent)
            }
            .padding()
            
            Spacer()
            
            // Scale sliders at bottom
            VStack(spacing: 8) {
                HStack {
                    Text("Face Buttons")
                        .frame(width: 120, alignment: .leading)
                    Slider(value: $settings.faceButtonScale, in: 0.3...1.5)
                    Text(String(format: "%.0f%%", settings.faceButtonScale * 100))
                        .frame(width: 50)
                }
                
                HStack {
                    Text("Triggers")
                        .frame(width: 120, alignment: .leading)
                    Slider(value: $settings.triggerScale, in: 0.3...1.5)
                    Text(String(format: "%.0f%%", settings.triggerScale * 100))
                        .frame(width: 50)
                }
                
                HStack {
                    Text("Joysticks")
                        .frame(width: 120, alignment: .leading)
                    Slider(value: $settings.joystickScale, in: 0.3...1.5)
                    Text(String(format: "%.0f%%", settings.joystickScale * 100))
                        .frame(width: 50)
                }
                
                HStack {
                    Text("Opacity")
                        .frame(width: 120, alignment: .leading)
                    Slider(value: $settings.buttonOpacity, in: 0.2...1.0)
                    Text(String(format: "%.0f%%", settings.buttonOpacity * 100))
                        .frame(width: 50)
                }
            }
            .font(.caption)
            .foregroundStyle(.white)
            .padding()
            .background(.black.opacity(0.7), in: RoundedRectangle(cornerRadius: 12))
            .padding()
        }
    }
}

/// Button using PNG image assets
struct ButtonImage: View {
    let name: String
    let button: Int32
    let size: CGFloat
    let opacity: CGFloat
    @State private var isPressed = false
    
    var body: some View {
        Image(isPressed ? "\(name)_pressed" : name, bundle: .main)
            .resizable()
            .aspectRatio(contentMode: .fit)
            .frame(width: size, height: size)
            .opacity(opacity)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if !isPressed {
                            isPressed = true
                            az_button_event(button, true)
                        }
                    }
                    .onEnded { _ in
                        isPressed = false
                        az_button_event(button, false)
                    }
            )
    }
}

/// Virtual D-Pad using PNG assets from Android
struct DPadView: View {
    let size: CGFloat
    @State private var currentDirection: Set<DPadDirection> = []
    
    var body: some View {
        ZStack {
            // Base dpad image with proper Android asset
            Image(imageName, bundle: .main)
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(width: size, height: size)
            
            // Invisible hit zones for each direction
            DPadHitZone(direction: .up, currentDirection: $currentDirection)
                .frame(width: size * 0.33, height: size * 0.33)
                .offset(y: -size * 0.33)
            
            DPadHitZone(direction: .down, currentDirection: $currentDirection)
                .frame(width: size * 0.33, height: size * 0.33)
                .offset(y: size * 0.33)
            
            DPadHitZone(direction: .left, currentDirection: $currentDirection)
                .frame(width: size * 0.33, height: size * 0.33)
                .offset(x: -size * 0.33)
            
            DPadHitZone(direction: .right, currentDirection: $currentDirection)
                .frame(width: size * 0.33, height: size * 0.33)
                .offset(x: size * 0.33)
        }
        .frame(width: size, height: size)
    }
    
    private var imageName: String {
        if currentDirection.isEmpty {
            return "DPad"
        } else if currentDirection.count == 2 {
            return "DPadPressed2"
        } else {
            return "DPadPressed1"
        }
    }
}

enum DPadDirection: Hashable {
    case up, down, left, right
    
    var button: Int32 {
        switch self {
        case .up: return Int32(AZ_DPAD_UP)
        case .down: return Int32(AZ_DPAD_DOWN)
        case .left: return Int32(AZ_DPAD_LEFT)
        case .right: return Int32(AZ_DPAD_RIGHT)
        }
    }
}

struct DPadHitZone: View {
    let direction: DPadDirection
    @Binding var currentDirection: Set<DPadDirection>
    
    var body: some View {
        Color.clear
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if !currentDirection.contains(direction) {
                            currentDirection.insert(direction)
                            az_button_event(direction.button, true)
                        }
                    }
                    .onEnded { _ in
                        currentDirection.remove(direction)
                        az_button_event(direction.button, false)
                    }
            )
    }
}

/// Virtual analog stick (circle-pad style) using Android assets
struct AnalogStickView: View {
    @Binding var position: CGPoint
    let onPositionChanged: (CGFloat, CGFloat) -> Void
    let size: CGFloat
    let isCirclePad: Bool  // true for main stick, false for C-stick
    
    @State private var isDragging = false
    
    private let maxRadius: CGFloat
    
    init(position: Binding<CGPoint>, 
         onPositionChanged: @escaping (CGFloat, CGFloat) -> Void,
         size: CGFloat,
         isCirclePad: Bool = true) {
        self._position = position
        self.onPositionChanged = onPositionChanged
        self.size = size
        self.isCirclePad = isCirclePad
        self.maxRadius = size * 0.35  // Max travel distance
    }
    
    var body: some View {
        ZStack {
            // Range circle (background)
            Image(isCirclePad ? "stick_main_range" : "stick_c_range", bundle: .main)
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(width: size, height: size)
            
            // Stick knob
            Image(isDragging ? 
                  (isCirclePad ? "stick_main_pressed" : "stick_c_pressed") :
                  (isCirclePad ? "stick_main" : "stick_c"), 
                  bundle: .main)
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(width: size * 0.5, height: size * 0.5)
                .offset(x: position.x, y: position.y)
        }
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { value in
                    isDragging = true
                    let dx = value.translation.width
                    let dy = value.translation.height
                    let distance = sqrt(dx * dx + dy * dy)
                    let clampedDistance = min(distance, maxRadius)
                    let angle = atan2(dy, dx)
                    let x = clampedDistance * cos(angle)
                    let y = clampedDistance * sin(angle)
                    position = CGPoint(x: x, y: y)
                    onPositionChanged(x, y)
                }
                .onEnded { _ in
                    isDragging = false
                    position = .zero
                    onPositionChanged(0, 0)
                }
        )
    }
}
