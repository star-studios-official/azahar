// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI
import UniformTypeIdentifiers

struct MelonDSSettingsView: View {
    // BIOS paths
    @AppStorage("melonds_arm9_bios_path") private var arm9BiosPath = ""
    @AppStorage("melonds_arm7_bios_path") private var arm7BiosPath = ""
    @AppStorage("melonds_dsi_arm9_bios_path") private var dsiArm9BiosPath = ""
    @AppStorage("melonds_dsi_arm7_bios_path") private var dsiArm7BiosPath = ""
    @AppStorage("melonds_firmware_path") private var firmwarePath = ""
    @AppStorage("melonds_use_custom_firmware") private var useCustomFirmware = false

    // GBA cart
    @AppStorage("melonds_gba_cart_path") private var gbaCartPath = ""
    @AppStorage("melonds_gba_save_path") private var gbaSavePath = ""

    // DS save
    @AppStorage("melonds_ds_save_path") private var dsSavePath = ""

    // Emulation
    @AppStorage("melonds_use_jit") private var useJIT = true
    @AppStorage("melonds_force_software_renderer") private var forceSoftwareRenderer = false
    @AppStorage("melonds_audio_enabled") private var audioEnabled = true
    @AppStorage("melonds_speed_limit") private var speedLimit = 100
    @AppStorage("melonds_dsi_mode") private var dsiMode = false
    @AppStorage("melonds_console_type") private var consoleType = 0 // 0=NDS, 1=DSi

    // Audio
    @AppStorage("melonds_audio_bit_depth") private var audioBitDepth = 0 // 0=auto, 1=10bit, 2=16bit
    @AppStorage("melonds_audio_interpolation") private var audioInterpolation = 1 // 0=none,1=linear,2=cosine,3=cubic,4=gaussian
    @AppStorage("melonds_output_sample_rate") private var outputSampleRate = 48000

    // WiFi / Online Play
    @AppStorage("melonds_wifi_enabled") private var wifiEnabled = true
    @AppStorage("melonds_wfc_dns_redirect") private var wfcDNSRedirect = true

    // Screen
    @AppStorage("melonds_screen_layout") private var screenLayout = 0
    @AppStorage("melonds_screen_swap") private var screenSwap = false
    @AppStorage("melonds_screen_rotation") private var screenRotation = 0

    // DSi NAND
    @AppStorage("melonds_dsi_nand_path") private var dsiNANDPath = ""

    // SPI/Firmware
    @AppStorage("melonds_settings_file") private var settingsFilePath = ""
    @AppStorage("melonds_wifi_settings") private var wifiSettingsPath = ""

    // State for file pickers
    @State private var showingARM9BIOSPicker = false
    @State private var showingARM7BIOSPicker = false
    @State private var showingDSIARM9BIOSPicker = false
    @State private var showingDSIARM7BIOSPicker = false
    @State private var showingFirmwarePicker = false
    @State private var showingGBACartPicker = false
    @State private var showingGBASavePicker = false
    @State private var showingDSSavePicker = false
    @State private var showingSettingsFilePicker = false
    @State private var showingWifiSettingsPicker = false
    @State private var showingDSiNANDPicker = false

    // Saved state label
    @State private var savedStateInfo: String = ""

    var body: some View {
        Form {
            // MARK: - BIOS Files
            section("BIOS Files") {
                biosRow(
                    title: "ARM9 BIOS",
                    subtitle: arm9BiosPath.isEmpty ? "Using FreeBIOS (built-in)" : fileName(from: arm9BiosPath),
                    isPresent: !arm9BiosPath.isEmpty,
                    showingPicker: $showingARM9BIOSPicker,
                    onClear: { arm9BiosPath = "" }
                )
                biosRow(
                    title: "ARM7 BIOS",
                    subtitle: arm7BiosPath.isEmpty ? "Using FreeBIOS (built-in)" : fileName(from: arm7BiosPath),
                    isPresent: !arm7BiosPath.isEmpty,
                    showingPicker: $showingARM7BIOSPicker,
                    onClear: { arm7BiosPath = "" }
                )

                Toggle("DSi Mode", isOn: $dsiMode)
                    .tint(.blue)

                if dsiMode {
                    biosRow(
                        title: "DSi ARM9 BIOS",
                        subtitle: dsiArm9BiosPath.isEmpty ? "Using FreeBIOS" : fileName(from: dsiArm9BiosPath),
                        isPresent: !dsiArm9BiosPath.isEmpty,
                        showingPicker: $showingDSIARM9BIOSPicker,
                        onClear: { dsiArm9BiosPath = "" }
                    )
                    biosRow(
                        title: "DSi ARM7 BIOS",
                        subtitle: dsiArm7BiosPath.isEmpty ? "Using FreeBIOS" : fileName(from: dsiArm7BiosPath),
                        isPresent: !dsiArm7BiosPath.isEmpty,
                        showingPicker: $showingDSIARM7BIOSPicker,
                        onClear: { dsiArm7BiosPath = "" }
                    )
                }

                Toggle("Use Custom Firmware", isOn: $useCustomFirmware)
                    .tint(.blue)

                if useCustomFirmware {
                    biosRow(
                        title: "Firmware",
                        subtitle: firmwarePath.isEmpty ? "Not selected" : fileName(from: firmwarePath),
                        isPresent: !firmwarePath.isEmpty,
                        showingPicker: $showingFirmwarePicker,
                        onClear: { firmwarePath = "" }
                    )
                }

                Text("BIOS files are optional. melonDS uses FreeBIOS when not provided. Real BIOS files improve compatibility with commercial games.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            // MARK: - GBA Cart (Slot-2)
            section("GBA Cart (Slot-2)") {
                biosRow(
                    title: "GBA ROM",
                    subtitle: gbaCartPath.isEmpty ? "No GBA cart inserted" : fileName(from: gbaCartPath),
                    isPresent: !gbaCartPath.isEmpty,
                    showingPicker: $showingGBACartPicker,
                    onClear: { gbaCartPath = ""; gbaSavePath = "" }
                )

                if !gbaCartPath.isEmpty {
                    biosRow(
                        title: "GBA Save (.sav)",
                        subtitle: gbaSavePath.isEmpty ? "Auto-detect (.sav alongside ROM)" : fileName(from: gbaSavePath),
                        isPresent: !gbaSavePath.isEmpty,
                        showingPicker: $showingGBASavePicker,
                        onClear: { gbaSavePath = "" }
                    )
                }

                Text("Insert a GBA ROM into the emulated Slot-2. Save files (.sav) should be placed alongside the ROM or selected manually. DS games that access Slot-2 (e.g. Pal Park, Pokemon) will use this.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            // MARK: - DS Save Management
            section("DS Save Management") {
                biosRow(
                    title: "Import DS Save (.sav)",
                    subtitle: dsSavePath.isEmpty ? "No save imported" : fileName(from: dsSavePath),
                    isPresent: !dsSavePath.isEmpty,
                    showingPicker: $showingDSSavePicker,
                    onClear: { dsSavePath = "" }
                )

                Text("Import a .sav file from a real DS or another emulator. The save will be loaded the next time the DS game is launched.")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                // Show current game's save info
                if !dsSavePath.isEmpty {
                    HStack {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                        Text("Save loaded: \(fileName(from: dsSavePath))")
                            .font(.caption)
                    }
                }
            }

            // MARK: - Emulation
            section("Emulation") {
                Toggle("Enable JIT Compilation", isOn: $useJIT)
                    .tint(.blue)

                if !useJIT {
                    HStack {
                        Image(systemName: "info.circle")
                            .foregroundStyle(.orange)
                        Text("Interpreter mode is slower but always available. Enable StikDebug JIT for best performance.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                Toggle("Force Software Renderer", isOn: $forceSoftwareRenderer)
                    .tint(.blue)

                if forceSoftwareRenderer {
                    Text("Software rendering is slower but more compatible.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Toggle("Audio", isOn: $audioEnabled)
                    .tint(.blue)

                VStack(alignment: .leading) {
                    Text("Speed Limit: \(speedLimit)%")
                    Slider(value: Binding(
                        get: { Double(speedLimit) },
                        set: { speedLimit = Int($0) }
                    ), in: 50...300, step: 10)
                }

                Picker("Console Type", selection: $consoleType) {
                    Text("Nintendo DS").tag(0)
                    Text("Nintendo DSi").tag(1)
                }
                .pickerStyle(.segmented)
            }

            // MARK: - Audio Settings
            section("Audio") {
                Picker("Bit Depth", selection: $audioBitDepth) {
                    Text("Auto").tag(0)
                    Text("10-bit").tag(1)
                    Text("16-bit").tag(2)
                }
                .pickerStyle(.segmented)

                Picker("Interpolation", selection: $audioInterpolation) {
                    Text("None").tag(0)
                    Text("Linear").tag(1)
                    Text("Cosine").tag(2)
                    Text("Cubic").tag(3)
                    Text("Gaussian").tag(4)
                }
                .pickerStyle(.menu)

                Picker("Output Sample Rate", selection: $outputSampleRate) {
                    Text("32704 Hz (DS native)").tag(32704)
                    Text("41100 Hz").tag(41100)
                    Text("44100 Hz").tag(44100)
                    Text("48000 Hz").tag(48000)
                }
            }

            // MARK: - WiFi / Online Play
            section("WiFi / Online Play") {
                Toggle("Enable WiFi Networking", isOn: $wifiEnabled)
                    .tint(.green)

                if wifiEnabled {
                    Toggle("Wiimmfi DNS Redirect", isOn: $wfcDNSRedirect)
                        .tint(.green)

                    VStack(alignment: .leading, spacing: 4) {
                        if wfcDNSRedirect {
                            HStack {
                                Image(systemName: "globe")
                                    .foregroundStyle(.green)
                                Text("DNS queries for Nintendo WFC games are resolved via the host network. For Wiimmfi, DS game WFC settings should point to the virtual DNS.")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        } else {
                            HStack {
                                Image(systemName: "exclamationmark.triangle")
                                    .foregroundStyle(.orange)
                                Text("DNS redirect is off. Online play with Wiimmfi requires DNS redirect.")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                } else {
                    HStack {
                        Image(systemName: "wifi.slash")
                            .foregroundStyle(.secondary)
                        Text("WiFi is disabled. DS games that require Nintendo WFC online play will not work.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                Text("melonDS provides a user-mode TCP/IP stack (libslirp) for DS online play. DS games that use Nintendo WFC can connect to the internet when enabled. Requires an internet connection on the host.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            // MARK: - Screen Layout
            section("Screen Layout") {
                Picker("Layout", selection: $screenLayout) {
                    Text("Vertical (Top/Bottom)").tag(0)
                    Text("Horizontal (Left/Right)").tag(1)
                    Text("Top Screen Only").tag(2)
                    Text("Bottom Screen Only").tag(3)
                    Text("Hybrid (Big Top + Small Bottom)").tag(4)
                }

                Toggle("Swap Screens", isOn: $screenSwap)
                    .tint(.blue)

                Picker("Rotation", selection: $screenRotation) {
                    Text("None").tag(0)
                    Text("90° CW").tag(1)
                    Text("180°").tag(2)
                    Text("90° CCW").tag(3)
                }
            }

            // MARK: - DSi NAND
            section("DSi NAND") {
                biosRow(
                    title: "DSi NAND Image",
                    subtitle: dsiNANDPath.isEmpty ? "No NAND image selected" : fileName(from: dsiNANDPath),
                    isPresent: !dsiNANDPath.isEmpty,
                    showingPicker: $showingDSiNANDPicker,
                    onClear: { dsiNANDPath = "" }
                )

                if !dsiNANDPath.isEmpty {
                    HStack {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                        Text("NAND loaded: \(fileName(from: dsiNANDPath))")
                            .font(.caption)
                    }
                }

                Text("A DSi NAND image enables DSi-exclusive features: DSiWare titles, DSi Camera, DSi Sound, and DSi-exclusive apps. The NAND must have the nocash footer (DSi eMMC CID/CPU) and match the region BIOS files above.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            // MARK: - Boot Options
            section("Boot Options") {
                Button(action: {
                    az_run_twl_ds_firmware()
                }) {
                    HStack {
                        Image(systemName: "power")
                            .foregroundStyle(.blue)
                        Text("Boot DS Firmware")
                    }
                }

                Text("Boot into the Nintendo DS firmware menu (like inserting a DS into the 3DS). Requires DS BIOS files above.")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                if !dsiNANDPath.isEmpty {
                    Button(action: {
                        az_run_twl_dsi_nand(dsiNANDPath)
                    }) {
                        HStack {
                            Image(systemName: "power")
                                .foregroundStyle(.purple)
                            Text("Boot DSi NAND")
                        }
                    }

                    Text("Boot into the Nintendo DSi firmware using the NAND image above. This loads the DSi menu with installed DSiWare titles.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                } else {
                    HStack {
                        Image(systemName: "lock.fill")
                            .foregroundStyle(.secondary)
                        Text("Select a DSi NAND image above to enable DSi NAND boot.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }

            // MARK: - SPI / Firmware Settings
            section("Firmware Settings") {
                biosRow(
                    title: "SPI Settings File",
                    subtitle: settingsFilePath.isEmpty ? "Using defaults" : fileName(from: settingsFilePath),
                    isPresent: !settingsFilePath.isEmpty,
                    showingPicker: $showingSettingsFilePicker,
                    onClear: { settingsFilePath = "" }
                )

                biosRow(
                    title: "WiFi Settings",
                    subtitle: wifiSettingsPath.isEmpty ? "Using defaults" : fileName(from: wifiSettingsPath),
                    isPresent: !wifiSettingsPath.isEmpty,
                    showingPicker: $showingWifiSettingsPicker,
                    onClear: { wifiSettingsPath = "" }
                )

                Text("SPI settings control the DS firmware settings (language, username, etc). WiFi settings are needed for local wireless emulation.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            // MARK: - About
            section("About") {
                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        Image(systemName: "gamecontroller")
                            .foregroundStyle(.blue)
                        Text("melonDS Core")
                            .font(.headline)
                    }
                    Text("DS/DSi emulation via melonDS. When the Home Menu launches a DS game, it switches to this emulator. DS BIOS files can be obtained from a real DS/DSi system.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text("For JIT: Enable StikDebug in the main app settings, then enable JIT here. StikJIT provides the best performance for DS games.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .navigationTitle("DS/DSi Settings")
        // BIOS file pickers
        .fileImporter(
            isPresented: $showingARM9BIOSPicker,
            allowedContentTypes: [.data, .item],
            allowsMultipleSelection: false
        ) { result in handleBIOSResult(result, pathBinding: $arm9BiosPath) }
        .fileImporter(
            isPresented: $showingARM7BIOSPicker,
            allowedContentTypes: [.data, .item],
            allowsMultipleSelection: false
        ) { result in handleBIOSResult(result, pathBinding: $arm7BiosPath) }
        .fileImporter(
            isPresented: $showingDSIARM9BIOSPicker,
            allowedContentTypes: [.data, .item],
            allowsMultipleSelection: false
        ) { result in handleBIOSResult(result, pathBinding: $dsiArm9BiosPath) }
        .fileImporter(
            isPresented: $showingDSIARM7BIOSPicker,
            allowedContentTypes: [.data, .item],
            allowsMultipleSelection: false
        ) { result in handleBIOSResult(result, pathBinding: $dsiArm7BiosPath) }
        .fileImporter(
            isPresented: $showingFirmwarePicker,
            allowedContentTypes: [.data, .item],
            allowsMultipleSelection: false
        ) { result in handleBIOSResult(result, pathBinding: $firmwarePath) }
        // GBA cart pickers
        .fileImporter(
            isPresented: $showingGBACartPicker,
            allowedContentTypes: [
                UTType(filenameExtension: "gba") ?? .data,
                .data
            ],
            allowsMultipleSelection: false
        ) { result in handleGBAResult(result) }
        .fileImporter(
            isPresented: $showingGBASavePicker,
            allowedContentTypes: [
                UTType(filenameExtension: "sav") ?? .data,
                .data
            ],
            allowsMultipleSelection: false
        ) { result in handleBIOSResult(result, pathBinding: $gbaSavePath) }
        // DS save picker
        .fileImporter(
            isPresented: $showingDSSavePicker,
            allowedContentTypes: [
                UTType(filenameExtension: "sav") ?? .data,
                .data
            ],
            allowsMultipleSelection: false
        ) { result in handleDSSaveResult(result) }
        // DSi NAND picker
        .fileImporter(
            isPresented: $showingDSiNANDPicker,
            allowedContentTypes: [
                UTType(filenameExtension: "bin") ?? .data,
                .data
            ],
            allowsMultipleSelection: false
        ) { result in handleBIOSResult(result, pathBinding: $dsiNANDPath) }
        // SPI/WiFi settings pickers
        .fileImporter(
            isPresented: $showingSettingsFilePicker,
            allowedContentTypes: [.data, .item],
            allowsMultipleSelection: false
        ) { result in handleBIOSResult(result, pathBinding: $settingsFilePath) }
        .fileImporter(
            isPresented: $showingWifiSettingsPicker,
            allowedContentTypes: [.data, .item],
            allowsMultipleSelection: false
        ) { result in handleBIOSResult(result, pathBinding: $wifiSettingsPath) }
    }

    // MARK: - Helpers

    private func section(_ title: String, @ViewBuilder content: () -> some View) -> some View {
        Section { content() } header: { Text(title) }
    }

    private func biosRow(
        title: String,
        subtitle: String,
        isPresent: Bool,
        showingPicker: Binding<Bool>,
        onClear: @escaping () -> Void
    ) -> some View {
        HStack {
            VStack(alignment: .leading) {
                Text(title)
                Text(subtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            Spacer()
            if isPresent {
                Button("Clear", role: .destructive) { onClear() }
                    .font(.caption)
            }
            Button("Browse") { showingPicker.wrappedValue = true }
                .font(.caption)
                .buttonStyle(.bordered)
        }
    }

    private func handleBIOSResult(_ result: Result<[URL], Error>, pathBinding: Binding<String>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }
            let accessing = url.startAccessingSecurityScopedResource()
            defer { if accessing { url.stopAccessingSecurityScopedResource() } }
            let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first!
            let dest = docs.appendingPathComponent("melonDS").appendingPathComponent(url.lastPathComponent)
            try? FileManager.default.createDirectory(at: docs.appendingPathComponent("melonDS"), withIntermediateDirectories: true)
            try? FileManager.default.removeItem(at: dest)
            try? FileManager.default.copyItem(at: url, to: dest)
            pathBinding.wrappedValue = dest.path
        case .failure(let error):
            AppLogger.error("File Import", message: "Failed: \(error.localizedDescription)")
        }
    }

    private func handleGBAResult(_ result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }
            let accessing = url.startAccessingSecurityScopedResource()
            defer { if accessing { url.stopAccessingSecurityScopedResource() } }
            let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first!
            let dest = docs.appendingPathComponent("melonDS").appendingPathComponent(url.lastPathComponent)
            try? FileManager.default.createDirectory(at: docs.appendingPathComponent("melonDS"), withIntermediateDirectories: true)
            try? FileManager.default.removeItem(at: dest)
            try? FileManager.default.copyItem(at: url, to: dest)
            gbaCartPath = dest.path

            // Auto-detect .sav file alongside the ROM
            let savPath = dest.deletingPathExtension().appendingPathExtension("sav").path
            if FileManager.default.fileExists(atPath: savPath) {
                gbaSavePath = savPath
                AppLogger.info("GBA Cart", message: "Auto-detected save: \(savPath)")
            }
        case .failure(let error):
            AppLogger.error("GBA Cart Import", message: "Failed: \(error.localizedDescription)")
        }
    }

    private func handleDSSaveResult(_ result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }
            let accessing = url.startAccessingSecurityScopedResource()
            defer { if accessing { url.stopAccessingSecurityScopedResource() } }
            let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first!
            let dest = docs.appendingPathComponent("melonDS").appendingPathComponent(url.lastPathComponent)
            try? FileManager.default.createDirectory(at: docs.appendingPathComponent("melonDS"), withIntermediateDirectories: true)
            try? FileManager.default.removeItem(at: dest)
            try? FileManager.default.copyItem(at: url, to: dest)
            dsSavePath = dest.path
            AppLogger.info("DS Save", message: "Imported save: \(dest.path)")
        case .failure(let error):
            AppLogger.error("DS Save Import", message: "Failed: \(error.localizedDescription)")
        }
    }

    private func fileName(from path: String) -> String {
        (path as NSString).lastPathComponent
    }
}
