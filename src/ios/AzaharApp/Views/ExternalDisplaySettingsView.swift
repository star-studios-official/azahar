// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI

/// Settings view for configuring the external display mode and layout.
struct ExternalDisplaySettingsView: View {
    @ObservedObject var displayManager: DisplayManager
    @State private var layoutPreset: LayoutPreset = .largeTopSmallBottom

    enum LayoutPreset: String, CaseIterable, Identifiable {
        case equalScreens = "Equal Screens"
        case largeTopSmallBottom = "Large Top + Small Bottom"
        case largeTopMediumBottom = "Large Top + Medium Bottom"

        var id: String { rawValue }

        var config: ExternalLayoutConfiguration {
            switch self {
            case .equalScreens:
                return .equalScreens
            case .largeTopSmallBottom:
                return .largeTopSmallBottom
            case .largeTopMediumBottom:
                return .largeTopMediumBottom
            }
        }
    }

    var body: some View {
        NavigationView {
            Form {
                // Connection Status
                Section {
                    HStack {
                        Image(systemName: displayManager.isExternalDisplayConnected
                              ? "checkmark.circle.fill" : "xmark.circle.fill")
                            .foregroundStyle(displayManager.isExternalDisplayConnected ? .green : .red)
                        Text(displayManager.isExternalDisplayConnected
                             ? "External display connected" : "No external display")
                        Spacer()
                        if displayManager.isExternalDisplayConnected {
                            Text("\(Int(displayManager.externalScreen?.bounds.width ?? 0))×\(Int(displayManager.externalScreen?.bounds.height ?? 0))")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }

                    Button {
                        displayManager.forceExternalDisplay()
                    } label: {
                        Label("Force External Display", systemImage: "tv.and.mediabox")
                    }
                    .disabled(displayManager.isExternalDisplayConnected)
                } header: {
                    Text("Connection")
                } footer: {
                    Text("Connect via AirPlay, HDMI, or USB-C to enable external display output.")
                }

                // Display Mode
                Section("Display Mode") {
                    ForEach(ExternalDisplayMode.allCases) { mode in
                        Button {
                            displayManager.displayMode = mode
                        } label: {
                            HStack {
                                Image(systemName: mode.systemImage)
                                    .frame(width: 28)
                                    .foregroundStyle(displayManager.displayMode == mode ? .blue : .secondary)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(mode.displayName)
                                        .foregroundStyle(.primary)
                                    Text(mode.description)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                        .lineLimit(2)
                                }
                                Spacer()
                                if displayManager.displayMode == mode {
                                    Image(systemName: "checkmark.circle.fill")
                                        .foregroundStyle(.blue)
                                }
                            }
                        }
                    }
                }

                // Layout Configuration (only for modes that use it)
                if displayManager.displayMode == .externalLargeTopSmallBottom
                    || displayManager.displayMode == .externalBothScreens {
                    Section("External Layout") {
                        Picker("Layout Preset", selection: $layoutPreset) {
                            ForEach(LayoutPreset.allCases) { preset in
                                Text(preset.rawValue).tag(preset)
                            }
                        }
                        .pickerStyle(.segmented)
                        .onChange(of: layoutPreset) { newPreset in
                            displayManager.externalLayout = newPreset.config
                        }

                        VStack(alignment: .leading, spacing: 4) {
                            Text("Top Screen Height: \(Int(displayManager.externalLayout.topHeightFraction * 100))%")
                                .font(.caption)
                            Slider(
                                value: Binding(
                                    get: { displayManager.externalLayout.topHeightFraction },
                                    set: { displayManager.externalLayout.topHeightFraction = $0 }
                                ),
                                in: 0.3...0.9,
                                step: 0.05
                            )
                        }

                        VStack(alignment: .leading, spacing: 4) {
                            Text("Bottom Screen Height: \(Int(displayManager.externalLayout.bottomHeightFraction * 100))%")
                                .font(.caption)
                            Slider(
                                value: Binding(
                                    get: { displayManager.externalLayout.bottomHeightFraction },
                                    set: { displayManager.externalLayout.bottomHeightFraction = $0 }
                                ),
                                in: 0.1...0.7,
                                step: 0.05
                            )
                        }

                        VStack(alignment: .leading, spacing: 4) {
                            Text("Spacing: \(Int(displayManager.externalLayout.spacing))pt")
                                .font(.caption)
                            Slider(
                                value: Binding(
                                    get: { displayManager.externalLayout.spacing },
                                    set: { displayManager.externalLayout.spacing = $0 }
                                ),
                                in: 0...40,
                                step: 4
                            )
                        }

                        VStack(alignment: .leading, spacing: 4) {
                            Text("Margin: \(Int(displayManager.externalLayout.margin))pt")
                                .font(.caption)
                            Slider(
                                value: Binding(
                                    get: { displayManager.externalLayout.margin },
                                    set: { displayManager.externalLayout.margin = $0 }
                                ),
                                in: 0...60,
                                step: 4
                            )
                        }
                    }
                }

                // iPhone Behavior
                if displayManager.displayMode != .iPhoneDualScreen {
                    Section("iPhone Behavior") {
                        let config = displayManager.configuration
                        HStack {
                            Image(systemName: "iphone")
                                .frame(width: 28)
                            VStack(alignment: .leading, spacing: 2) {
                                Text("iPhone Display")
                                    .font(.subheadline)
                                if config.iPhoneShowsTopScreen || config.iPhoneShowsBottomScreen {
                                    Text(screenDescription(config))
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                } else {
                                    Text("Controls only (touch input active)")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                            }
                        }
                    }
                }

                // Layout Preview
                if displayManager.isExternalDisplayConnected {
                    Section("Preview") {
                        LayoutPreviewView(
                            mode: displayManager.displayMode,
                            layout: displayManager.externalLayout
                        )
                        .frame(height: 200)
                        .clipShape(RoundedRectangle(cornerRadius: 12))
                    }
                }
            }
            .navigationTitle("External Display")
            .navigationBarTitleDisplayMode(.inline)
            .onAppear {
                // Sync layout preset from current config.
                syncLayoutPreset()
            }
        }
    }

    // MARK: - Helpers

    private func screenDescription(_ config: DisplayConfiguration) -> String {
        var screens = [String]()
        if config.iPhoneShowsTopScreen { screens.append("Top") }
        if config.iPhoneShowsBottomScreen { screens.append("Bottom") }
        return screens.joined(separator: " + ") + " screen"
    }

    private func syncLayoutPreset() {
        let top = displayManager.externalLayout.topHeightFraction
        let bottom = displayManager.externalLayout.bottomHeightFraction

        if abs(top - 0.5) < 0.01 && abs(bottom - 0.5) < 0.01 {
            layoutPreset = .equalScreens
        } else if abs(top - 0.6) < 0.01 && abs(bottom - 0.4) < 0.01 {
            layoutPreset = .largeTopMediumBottom
        } else {
            layoutPreset = .largeTopSmallBottom
        }
    }
}

// MARK: - Layout Preview

/// Small visual preview showing how screens are arranged on the external display.
struct LayoutPreviewView: View {
    let mode: ExternalDisplayMode
    let layout: ExternalLayoutConfiguration

    var body: some View {
        GeometryReader { geometry in
            let size = geometry.size
            ZStack {
                Color.black

                switch mode {
                case .iPhoneDualScreen:
                    // No external rendering.
                    Text("No external output")
                        .foregroundStyle(.secondary)
                        .font(.caption)

                case .externalTopScreen:
                    // Just the top screen, centered.
                    let frame = LayoutCalculator.aspectFit(
                        aspectRatio: LayoutCalculator.topAspectRatio,
                        into: CGSize(width: size.width * 0.85, height: size.height * 0.85)
                    )
                    RoundedRectangle(cornerRadius: 4)
                        .fill(Color.blue.opacity(0.3))
                        .frame(width: frame.width, height: frame.height)
                        .overlay(Text("Top").font(.caption2))
                        .position(x: size.width / 2, y: size.height / 2)

                case .externalBothScreens:
                    // Both screens stacked equally.
                    let frames = LayoutCalculator.calculateFrames(for: size, layout: .equalScreens)
                    RoundedRectangle(cornerRadius: 4)
                        .fill(Color.blue.opacity(0.3))
                        .frame(width: frames.top.size.width, height: frames.top.size.height)
                        .overlay(Text("Top").font(.caption2))
                        .position(frames.top.centerX, frames.top.centerY)
                    RoundedRectangle(cornerRadius: 4)
                        .fill(Color.green.opacity(0.3))
                        .frame(width: frames.bottom.size.width, height: frames.bottom.size.height)
                        .overlay(Text("Bottom").font(.caption2))
                        .position(frames.bottom.centerX, frames.bottom.centerY)

                case .externalLargeTopSmallBottom:
                    let frames = LayoutCalculator.calculateLargeTopSmallBottom(for: size, layout: layout)
                    RoundedRectangle(cornerRadius: 4)
                        .fill(Color.blue.opacity(0.3))
                        .frame(width: frames.top.size.width, height: frames.top.size.height)
                        .overlay(Text("Top").font(.caption2))
                        .position(frames.top.centerX, frames.top.centerY)
                    RoundedRectangle(cornerRadius: 4)
                        .fill(Color.green.opacity(0.3))
                        .frame(width: frames.bottom.size.width, height: frames.bottom.size.height)
                        .overlay(Text("Bottom").font(.caption2))
                        .position(frames.bottom.centerX, frames.bottom.centerY)

                case .mirror:
                    Text("Mirror")
                        .foregroundStyle(.secondary)
                        .font(.caption)
                }
            }
        }
        .background(.black)
    }
}
