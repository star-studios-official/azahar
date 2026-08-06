// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI

// MARK: - Helper Functions

func safeString(from cString: UnsafePointer<Int8>?) -> String {
    guard let cString = cString else { return "Unknown" }
    return String(cString: cString)
}

/// Settings screen (equivalent to Android's SettingsActivity/SettingsFragment).

struct SettingsView: View {
    @EnvironmentObject var appState: AppState
    @Environment(\.dismiss) private var dismiss
    @AppStorage("last_artic_base_addr") private var lastArticBaseAddr = ""
    @State private var showArticBaseDialog = false
    @State private var articBaseAddress = ""

    var body: some View {
        NavigationStack {
            List {
                settingsSection("Core", icon: "cpu") {
                    Text("FastInterp (optimized cached interpreter) is used by default. JIT can be enabled if you have StikDebug or proper code signing.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .padding(.vertical, 4)
                    
                    SettingToggle(
                        title: "Use CPU JIT",
                        description: "Just-in-Time compiler for CPU. Requires StikDebug or development signing. FastInterp used if unavailable.",
                        group: "Core", key: "use_cpu_jit"
                    )
                    
                    SettingToggle(
                        title: "Use FastInterp",
                        description: "Optimized cached interpreter. Disable to use legacy DynCom interpreter (slower).",
                        group: "Core", key: "use_fastinterp"
                    )
                    
                    SettingSlider(
                        title: "CPU Clock Percentage",
                        description: "Underclocking can improve performance at risk of freezing. Overclocking may fix lag. Default: 100%",
                        group: "Core", key: "cpu_clock_percentage",
                        range: 25...400, step: 1
                    )
                    
                    SettingToggle(
                        title: "New 3DS Mode",
                        description: "Enable New 3DS features (faster CPU, more RAM)",
                        group: "System", key: "is_new_3ds"
                    )
                    
                    SettingToggle(
                        title: "3GX Plugin Loader",
                        description: "Enable support for 3GX plugins",
                        group: "System", key: "plugin_loader_enabled"
                    )
                }

                settingsSection("Renderer", icon: "display") {
                    SettingPicker(
                        title: "Graphics API",
                        group: "Renderer", key: "graphics_api",
                        options: [
                            (1, "OpenGL (unsupported on iOS)"),
                            (2, "Vulkan")
                        ]
                    )
                    
                    SettingToggle(
                        title: "Use Shader JIT",
                        description: "JIT for software shader emulation. Requires StikDebug or development signing.",
                        group: "Renderer", key: "use_shader_jit"
                    )
                    
                    SettingToggle(
                        title: "Hardware Shaders",
                        description: "Uses GPU to emulate 3DS shaders. Recommended for better performance.",
                        group: "Renderer", key: "use_hw_shader"
                    )
                    
                    SettingToggle(
                        title: "Accurate Multiplication",
                        description: "More accurate but slower shader multiplication. Fix issues in some games.",
                        group: "Renderer", key: "shaders_accurate_mul"
                    )
                    
                    SettingSlider(
                        title: "Resolution Scale",
                        description: "0 = Auto, 1 = Native (400x240), higher = upscaled",
                        group: "Renderer", key: "resolution_factor",
                        range: 0...10, step: 0.5
                    )
                    
                    SettingToggle(
                        title: "VSync",
                        description: "Synchronize frame output to display refresh rate",
                        group: "Renderer", key: "use_vsync"
                    )
                    
                    SettingToggle(
                        title: "Skip Duplicate Frames",
                        description: "Improves performance in 30fps games",
                        group: "Renderer", key: "use_skip_duplicate_frames"
                    )
                    
                    SettingToggle(
                        title: "Disk Shader Cache",
                        description: "Reduce stuttering by caching compiled shaders",
                        group: "Renderer", key: "use_disk_shader_cache"
                    )
                    
                    SettingToggle(
                        title: "Async Shader Compilation",
                        description: "Compile shaders on background threads (Vulkan only)",
                        group: "Renderer", key: "async_shader_compilation"
                    )
                }

                settingsSection("Layout", icon: "rectangle.split.2x2") {
                    SettingPicker(
                        title: "Screen Layout",
                        group: "Layout", key: "layout_option",
                        options: [
                            (0, "Original"),
                            (1, "Single Screen"),
                            (2, "Large Screen"),
                            (3, "Side by Side"),
                            (4, "Hybrid"),
                            (5, "Custom"),
                        ]
                    )
                    SettingToggle(
                        title: "Swap Screens",
                        group: "Layout", key: "swap_screen"
                    )
                    SettingSlider(
                        title: "Screen Gap",
                        group: "Layout", key: "screen_gap",
                        range: 0...50, step: 1
                    )
                    SettingSlider(
                        title: "Large Screen Proportion",
                        group: "Layout", key: "large_screen_proportion",
                        range: 1.0...5.0, step: 0.25
                    )
                }
                
                // External Display (AirPlay/HDMI)
                settingsSection("External Display", icon: "tv") {
                    ExternalDisplaySettingsSection()
                }
                
                // Controller Settings
                settingsSection("Controller", icon: "gamecontroller.fill") {
                    NavigationLink {
                        ControllerRemapperView()
                    } label: {
                        Label("Controller Mapping", systemImage: "gamecontroller")
                    }
                    
                    Text("Customize button mappings and configure adaptive triggers for PS5 DualSense controllers")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                settingsSection("Audio", icon: "speaker.wave.2") {
                    SettingToggle(
                        title: "Enable Audio",
                        group: "Audio", key: "audio_emulation"
                    )
                    SettingSlider(
                        title: "Volume",
                        group: "Audio", key: "volume",
                        range: 0...1, step: 0.05
                    )
                    SettingToggle(
                        title: "Enable Audio Stretching",
                        description: "Stretches audio to maintain sync with video",
                        group: "Audio", key: "enable_audio_stretching"
                    )
                    SettingToggle(
                        title: "Enable Realtime Audio",
                        description: "Scales audio playback speed to account for drops in emulation framerate",
                        group: "Audio", key: "enable_realtime_audio"
                    )
                    SettingToggle(
                        title: "Simulate Headphones",
                        description: "Simulates whether headphones are plugged in",
                        group: "Audio", key: "simulate_headphones_plugged"
                    )
                    SettingPicker(
                        title: "Audio Input Device",
                        group: "Audio", key: "input_type",
                        options: [
                            (0, "Auto"),
                            (1, "None"),
                            (2, "Static Noise"),
                            (3, "Real (Cubeb)"),
                            (4, "Real (OpenAL)")
                        ]
                    )
                }

                settingsSection("System", icon: "gear") {
                    SettingPicker(
                        title: "System Model",
                        group: "System", key: "is_new_3ds",
                        options: [(0, "Old 3DS"), (1, "New 3DS")]
                    )
                    SettingPicker(
                        title: "Region",
                        group: "System", key: "region_value",
                        options: [
                            (-1, "Auto-select"),
                            (0, "Japan"), (1, "USA"), (2, "Europe"),
                            (3, "Australia"), (4, "China"), (5, "Korea"),
                            (6, "Taiwan"),
                        ]
                    )
                    SettingPicker(
                        title: "Init Clock",
                        group: "System", key: "init_clock",
                        options: [(0, "System clock"), (1, "Fixed time")]
                    )
                    SettingToggle(
                        title: "Enable LLE Modules",
                        description: "Use system modules from your 3DS (better compatibility)",
                        group: "Core", key: "enable_required_online_lle_modules"
                    )
                    SettingToggle(
                        title: "Delay Start for LLE",
                        description: "Wait for LLE modules to load before starting",
                        group: "Debugging", key: "delay_start_for_lle_modules"
                    )
                }

                settingsSection("Stereoscopic 3D", icon: "scope") {
                    SettingPicker(
                        title: "3D Rendering",
                        group: "Renderer", key: "render_3d",
                        options: [
                            (0, "Off"), (1, "Half-width Side by Side"),
                            (2, "Full-width Side by Side"),
                            (3, "Anaglyph"), (4, "Interlaced"),
                        ]
                    )
                    SettingSlider(
                        title: "3D Factor",
                        group: "Renderer", key: "factor_3d",
                        range: 0...1, step: 0.05
                    )
                }

                settingsSection("Storage", icon: "internaldrive") {
                    SettingToggle(
                        title: "Virtual SD Card",
                        group: "Data Storage", key: "use_virtual_sd"
                    )
                    SettingToggle(
                        title: "Compress CIA Installs",
                        group: "Storage", key: "compress_cia_installs"
                    )
                }

                settingsSection("Textures", icon: "photo") {
                    SettingToggle(title: "Custom Textures", group: "Utility", key: "custom_textures")
                    SettingToggle(title: "Preload Textures", group: "Utility", key: "preload_textures")
                    SettingToggle(title: "Dump Textures", group: "Utility", key: "dump_textures")
                }

                settingsSection("Debugging", icon: "ant") {
                    NavigationLink {
                        LogViewerView()
                    } label: {
                        Label("View Logs", systemImage: "doc.text.magnifyingglass")
                    }
                    
                    SettingToggle(
                        title: "Experimental Logging",
                        description: "Log ALL Swift UI events, navigation, and actions to log file",
                        group: "Debugging", key: "experimental_logging"
                    )
                    
                    SettingToggle(title: "Renderer Debug", group: "Debugging", key: "renderer_debug")
                    SettingToggle(title: "Frame Time Recording", group: "Debugging", key: "record_frame_times")
                    
                    SettingPicker(
                        title: "C++ Log Level",
                        group: "Debugging", key: "log_filter_level",
                        options: [
                            (0, "Trace (All)"),
                            (1, "Debug (All)"),
                            (2, "Info"),
                            (3, "Warning"),
                            (4, "Error"),
                            (5, "Critical")
                        ]
                    )
                    
                    SettingToggle(
                        title: "Show Config Loading Logs",
                        description: "Display config.ini load messages in logs",
                        group: "Debugging", key: "log_config_loading"
                    )
                    
                    SettingToggle(
                        title: "Log Renderer",
                        description: "Enable detailed graphics renderer logging",
                        group: "Debugging", key: "log_renderer"
                    )
                    
                    SettingToggle(
                        title: "Log Stack Traces",
                        description: "Include stack traces in crash reports",
                        group: "Debugging", key: "log_stack_trace"
                    )
                    
                    Text("Experimental Logging captures every UI interaction and navigation event. Game operations and errors are always logged. Debug/Trace levels capture ALL log categories.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                settingsSection("Network", icon: "network") {
                    Button {
                        showArticBaseDialog = true
                    } label: {
                        HStack {
                            Label("Connect to Artic Base", systemImage: "wifi")
                            Spacer()
                            Image(systemName: "chevron.right")
                                .foregroundStyle(.secondary)
                                .font(.caption)
                        }
                    }
                    .foregroundStyle(.primary)
                    
                    Text("Connect to a real 3DS console running Artic Base server for online play")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                settingsSection("RetroAchievements", icon: "trophy") {
                    NavigationLink {
                        RetroAchievementsView()
                    } label: {
                        Label("Account & Login", systemImage: "person.circle")
                    }
                    
                    NavigationLink {
                        AchievementListView()
                    } label: {
                        Label("Achievements", systemImage: "trophy")
                    }
                    
                    NavigationLink {
                        LeaderboardListView()
                    } label: {
                        Label("Leaderboards", systemImage: "chart.bar")
                    }
                    
                    Toggle(isOn: Binding(
                        get: { az_ra_is_hardcore_enabled() },
                        set: { az_ra_set_hardcore_enabled($0) }
                    )) {
                        VStack(alignment: .leading) {
                            Text("Hardcore Mode")
                            Text("Disables save states and cheats for full challenge")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                    
                    Text("Track achievements and compete on leaderboards")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                settingsSection("JIT Compilation", icon: "bolt.fill") {
                    NavigationLink {
                        JITSettingsView()
                    } label: {
                        Label("JIT Settings", systemImage: "bolt.circle")
                    }
                    
                    Text("Enable Just-In-Time compilation for better performance")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                settingsSection("System Files", icon: "internaldrive") {
                    NavigationLink {
                        SystemFilesView()
                    } label: {
                        Label("Manage System Files", systemImage: "folder")
                    }
                    
                    NavigationLink {
                        SystemFilesDownloaderView()
                    } label: {
                        Label("Download System Files", systemImage: "arrow.down.circle")
                    }
                    
                    Text("Install Home Menu, shared fonts, and system archives from your 3DS")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                settingsSection("About", icon: "info.circle") {
                    HStack {
                        Text("Version")
                        Spacer()
                        Text(safeString(from: az_get_version_string()))
                            .foregroundStyle(.secondary)
                    }
                    
                    NavigationLink {
                        SystemInfoView()
                    } label: {
                        Label("System Information", systemImage: "info.circle.fill")
                    }
                    
                    NavigationLink("Licenses") {
                        Text("GPLv2+")
                            .navigationTitle("License")
                    }
                }
            }
            .navigationTitle("Settings")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
            .alert("Connect to Artic Base", isPresented: $showArticBaseDialog) {
                TextField("Server Address", text: $articBaseAddress)
                Button("Connect") {
                    if !articBaseAddress.isEmpty {
                        lastArticBaseAddr = articBaseAddress
                        // Launch emulation with articbase:// URL
                        let game = Game(
                            path: "articbase://\(articBaseAddress)",
                            title: "Artic Base",
                            titleId: 0,
                            mediaType: Int32(AZ_MEDIA_TYPE_SDMC)
                        )
                        appState.currentGame = game
                        appState.isEmulating = true
                        dismiss()
                    }
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Enter the IP address of the Artic Base server running on your 3DS console")
            }
            .onAppear {
                articBaseAddress = lastArticBaseAddr
            }
        }
    }

    @ViewBuilder
    private func settingsSection(_ title: String, icon: String,
                                  @ViewBuilder content: () -> some View) -> some View {
        Section {
            content()
        } header: {
            Label(title, systemImage: icon)
        }
    }
}

// MARK: - Reusable setting components

/// A toggle that reads/writes a boolean setting via the bridge.
struct SettingToggle: View {
    let title: String
    var description: String? = nil
    let group: String
    let key: String
    @State private var isOn: Bool = false

    var body: some View {
        Toggle(isOn: $isOn) {
            VStack(alignment: .leading) {
                Text(title)
                if let description {
                    Text(description).font(.caption).foregroundStyle(.secondary)
                }
            }
        }
        .onChange(of: isOn) { _, newValue in
            az_setting_set_bool(group, key, newValue)
        }
        .onAppear {
            isOn = az_setting_get_bool(group, key, false)
        }
    }
}

/// A picker that reads/writes an integer setting via the bridge.
struct SettingPicker: View {
    let title: String
    let group: String
    let key: String
    let options: [(Int, String)]
    @State private var selection: Int = 0

    var body: some View {
        Picker(title, selection: $selection) {
            ForEach(options, id: \.0) { value, label in
                Text(label).tag(value)
            }
        }
        .onChange(of: selection) { _, newValue in
            az_setting_set_int(group, key, newValue)
            
            // Special handling for log_filter_level - apply immediately
            if key == "log_filter_level" {
                az_apply_log_filter_level(Int32(newValue))
                print("[Settings] Applied log filter level: \(newValue)")
            }
        }
        .onAppear {
            selection = Int(az_setting_get_int(group, key, options.first?.0 ?? 0))
        }
    }
}

/// A slider that reads/writes a float/double setting via the bridge.
struct SettingSlider: View {
    let title: String
    var description: String? = nil
    let group: String
    let key: String
    let range: ClosedRange<Double>
    var step: Double = 1
    @State private var value: Double = 0

    var body: some View {
        VStack(alignment: .leading) {
            HStack {
                Text(title)
                Spacer()
                Text(String(format: "%.1f", value))
                    .foregroundStyle(.secondary)
                    .monospacedDigit()
            }
            if let description {
                Text(description).font(.caption).foregroundStyle(.secondary)
            }
            Slider(value: $value, in: range, step: step)
                .onChange(of: value) { _, newValue in
                    az_setting_set_float(group, key, newValue)
                }
                .onAppear {
                    value = az_setting_get_float(group, key, range.lowerBound)
                }
        }
    }
}

/// External Display settings section
struct ExternalDisplaySettingsSection: View {
    @ObservedObject var displayManager = ExternalDisplayManager.shared
    
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            // Connection status
            HStack {
                Image(systemName: displayManager.isExternalDisplayConnected ? "tv.fill" : "tv")
                    .foregroundStyle(displayManager.isExternalDisplayConnected ? .green : .secondary)
                Text(displayManager.isExternalDisplayConnected ? "External Display Connected" : "No External Display")
                    .foregroundStyle(displayManager.isExternalDisplayConnected ? .primary : .secondary)
            }
            .font(.subheadline)
            .padding(.vertical, 4)
            
            if displayManager.isExternalDisplayConnected {
                // Display mode picker
                Picker("Display Mode", selection: Binding(
                    get: { displayManager.displayMode },
                    set: { displayManager.setDisplayMode($0) }
                )) {
                    ForEach(ExternalDisplayManager.ExternalDisplayMode.allCases, id: \.self) { mode in
                        Text(mode.displayName).tag(mode)
                    }
                }
                .pickerStyle(.menu)
                
                // Description of current mode
                Text(displayManager.displayMode.description)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .padding(.vertical, 4)
            } else {
                Text("Connect via AirPlay, HDMI, or USB-C to use an external display")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .padding(.vertical, 4)
            }
        }
    }
}
