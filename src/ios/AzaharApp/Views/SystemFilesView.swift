// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI
import UniformTypeIdentifiers

/// System files management (equivalent to Android's SystemFilesFragment).
/// Allows users to manage NAND titles, system archives, and unique data.
struct SystemFilesView: View {
    @EnvironmentObject var appState: AppState
    @State private var isLinked = false
    @State private var systemTitlesAvailable = false
    @State private var isLoading = false
    @State private var showingAlert = false
    @State private var alertMessage = ""
    @State private var showingZipPassExport = false
    @State private var showingZipPassImport = false
    @State private var showingCIAImport = false
    @State private var installProgress: Double = 0
    @State private var isInstalling = false
    @State private var showHomeMenuPicker = false
    @State private var selectedRegion = 1 // Default to USA
    
    private let regionNames = ["Japan", "USA", "Europe", "Australia", "China", "Korea", "Taiwan"]
    
    private var isHomeMenuAvailable: Bool {
        az_home_menu_available()
    }

    var body: some View {
        List {
            Section("Console Status") {
                HStack {
                    Label("Console Linked", systemImage: "link")
                    Spacer()
                    Text(isLinked ? "Yes" : "No")
                        .foregroundStyle(.secondary)
                }
                HStack {
                    Label("3DS System Titles", systemImage: "internaldrive")
                    Spacer()
                    Text(systemTitlesAvailable ? "Installed" : "Not found")
                        .foregroundStyle(.secondary)
                }
            }

            Section("System Files") {
                Button {
                    showingCIAImport = true
                } label: {
                    Label("Install System CIA", systemImage: "arrow.down.doc")
                }
                
                if isInstalling {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("Installing...")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        ProgressView(value: installProgress)
                    }
                }
                
                Text("Install 3DS system files (Home Menu, Mii Maker, etc.) from CIA files. You can obtain these from your own 3DS console.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Section("StreetPass (ZipPass)") {
                Button {
                    exportZipPass()
                } label: {
                    Label("Export StreetPass Data", systemImage: "square.and.arrow.up")
                }
                
                Button {
                    showingZipPassImport = true
                } label: {
                    Label("Import StreetPass Data", systemImage: "square.and.arrow.down")
                }
                
                Button(role: .destructive) {
                    clearStreetPassData()
                } label: {
                    Label("Clear StreetPass Data", systemImage: "trash")
                }
                
                Text("Export and import StreetPass Mii Plaza data to share with other devices or backup your progress.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Section("System Archives") {
                if isLoading {
                    ProgressView("Loading...")
                } else {
                    ForEach(SystemArchiveType.allCases) { archive in
                        HStack {
                            Label(archive.displayName, systemImage: archive.icon)
                            Spacer()
                            if archive.isInstalled {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundStyle(.green)
                            } else {
                                Image(systemName: "xmark.circle.fill")
                                    .foregroundStyle(.red)
                            }
                        }
                    }
                    
                    // Seeddb.bin status
                    HStack {
                        Label("Seed Database", systemImage: "server.rack")
                        Spacer()
                        if az_seeddb_available() {
                            Image(systemName: "checkmark.circle.fill")
                                .foregroundStyle(.green)
                        } else {
                            Image(systemName: "xmark.circle.fill")
                                .foregroundStyle(.red)
                        }
                    }
                }
            }
            
            Section {
                // Boot9 bootrom
                HStack {
                    Label("Boot9 Bootrom", systemImage: "cpu")
                    Spacer()
                    if az_bootrom9_available() {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                    } else {
                        Image(systemName: "xmark.circle.fill")
                            .foregroundStyle(.gray)
                    }
                }
                
                // Boot11 bootrom
                HStack {
                    Label("Boot11 Bootrom", systemImage: "cpu")
                    Spacer()
                    if az_bootrom11_available() {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                    } else {
                        Image(systemName: "xmark.circle.fill")
                            .foregroundStyle(.gray)
                    }
                }
                
                // Secret Sector
                HStack {
                    Label("Secret Sector", systemImage: "lock.shield")
                    Spacer()
                    if az_secret_sector_available() {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                    } else {
                        Image(systemName: "xmark.circle.fill")
                            .foregroundStyle(.gray)
                    }
                }
                
                // DSP Firmware
                HStack {
                    Label("DSP Firmware", systemImage: "waveform")
                    Spacer()
                    if az_dsp_firmware_available() {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                    } else {
                        Image(systemName: "xmark.circle.fill")
                            .foregroundStyle(.gray)
                    }
                }
                
                Text("These files are optional and not required for most games. They enable advanced features and improve compatibility.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            } header: {
                Text("Optional System Files")
            }

            Section("Actions") {
                Button {
                    showHomeMenuPicker = true
                } label: {
                    Label("Boot Home Menu", systemImage: "house")
                }
                .disabled(!isHomeMenuAvailable)
                
                Button {
                    let keysAvailable = az_are_keys_available()
                    alertMessage = keysAvailable
                        ? "AES keys are available."
                        : "AES keys not available. System archives may not work."
                    showingAlert = true
                } label: {
                    Label("Check AES Keys", systemImage: "key")
                }

                if isLinked {
                    Button(role: .destructive) {
                        az_unlink_console()
                        isLinked = az_is_full_console_linked()
                    } label: {
                        Label("Unlink Console", systemImage: "link.badge.xmark")
                    }
                }
            }
        }
        .navigationTitle("System Files")
        .navigationBarTitleDisplayMode(.inline)
        .onAppear {
            // Initialize system save data (mirrors Android SystemSaveGame.load())
            az_init_system_save_data()
            checkSystemStatus()
        }
        .alert("System Files", isPresented: $showingAlert) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(alertMessage)
        }
        .confirmationDialog("Select Region", isPresented: $showHomeMenuPicker) {
            ForEach(0..<7, id: \.self) { region in
                if az_system_files_region_available(Int32(region)) {
                    Button(regionNames[region]) {
                        bootHomeMenu(region: region)
                    }
                }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Choose which region's Home Menu to boot")
        }
        .fileImporter(
            isPresented: $showingCIAImport,
            allowedContentTypes: [UTType(filenameExtension: "cia") ?? .data],
            allowsMultipleSelection: false
        ) { result in
            handleCIAImport(result)
        }
        .fileExporter(
            isPresented: $showingZipPassExport,
            document: ZipPassDocument(),
            contentType: .zip,
            defaultFilename: "streetpass_\(Int(Date().timeIntervalSince1970)).zip"
        ) { result in
            handleZipPassExport(result)
        }
        .fileImporter(
            isPresented: $showingZipPassImport,
            allowedContentTypes: [.zip],
            allowsMultipleSelection: false
        ) { result in
            handleZipPassImport(result)
        }
    }
    
    private func checkSystemStatus() {
        isLinked = az_is_full_console_linked()
        systemTitlesAvailable = az_system_files_available()
    }
    
    private func handleCIAImport(_ result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }
            
            guard url.startAccessingSecurityScopedResource() else {
                alertMessage = "Failed to access file"
                showingAlert = true
                return
            }
            defer { url.stopAccessingSecurityScopedResource() }
            
            // Copy the CIA file to a temporary location within app's sandbox
            // This is necessary because File Provider paths may not be accessible
            // after the security scoped resource is released
            let tempDir = FileManager.default.temporaryDirectory
            let tempURL = tempDir.appendingPathComponent(url.lastPathComponent)
            
            do {
                // Remove existing temp file if it exists
                if FileManager.default.fileExists(atPath: tempURL.path) {
                    try FileManager.default.removeItem(at: tempURL)
                }
                
                // Copy the file to temp location
                try FileManager.default.copyItem(at: url, to: tempURL)
                
                isInstalling = true
                installProgress = 0
                
                DispatchQueue.global(qos: .userInitiated).async {
                    let result = az_install_cia(tempURL.path)
                    
                    // Clean up temp file after installation
                    try? FileManager.default.removeItem(at: tempURL)
                    
                    DispatchQueue.main.async {
                        isInstalling = false
                        
                        if result == 0 {
                            alertMessage = "System file installed successfully!"
                            checkSystemStatus()
                        } else {
                            alertMessage = "Failed to install system file. Error code: \(result)"
                        }
                        showingAlert = true
                    }
                }
            } catch {
                alertMessage = "Failed to copy CIA file: \(error.localizedDescription)"
                showingAlert = true
            }
            
        case .failure(let error):
            alertMessage = "Failed to import CIA: \(error.localizedDescription)"
            showingAlert = true
        }
    }
    
    private func exportZipPass() {
        showingZipPassExport = true
    }
    
    private func handleZipPassExport(_ result: Result<URL, Error>) {
        switch result {
        case .success(let url):
            guard url.startAccessingSecurityScopedResource() else {
                alertMessage = "Failed to access export location"
                showingAlert = true
                return
            }
            defer { url.stopAccessingSecurityScopedResource() }
            
            let result = az_zippass_export(url.path)
            
            if result == 0 {
                alertMessage = "StreetPass data exported successfully!"
            } else {
                alertMessage = "Failed to export StreetPass data. Error code: \(result)"
            }
            showingAlert = true
            
        case .failure(let error):
            alertMessage = "Failed to export: \(error.localizedDescription)"
            showingAlert = true
        }
    }
    
    private func handleZipPassImport(_ result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }
            
            guard url.startAccessingSecurityScopedResource() else {
                alertMessage = "Failed to access file"
                showingAlert = true
                return
            }
            defer { url.stopAccessingSecurityScopedResource() }
            
            let result = az_zippass_import(url.path)
            
            if result == 0 {
                alertMessage = "StreetPass data imported successfully!"
            } else {
                alertMessage = "Failed to import StreetPass data. Error code: \(result)"
            }
            showingAlert = true
            
        case .failure(let error):
            alertMessage = "Failed to import: \(error.localizedDescription)"
            showingAlert = true
        }
    }
    
    private func clearStreetPassData() {
        let result = az_zippass_clear_config()
        
        if result == 0 {
            alertMessage = "StreetPass data cleared successfully!"
        } else {
            alertMessage = "Failed to clear StreetPass data. Error code: \(result)"
        }
        showingAlert = true
    }
    
    private func bootHomeMenu(region: Int) {
        let path = String(cString: az_get_home_menu_path(Int32(region)))
        
        guard !path.isEmpty else {
            alertMessage = "Home Menu for selected region not found"
            showingAlert = true
            return
        }
        
        // Initialize system save data (CFG archive, config file, etc.)
        // before booting - mirrors Android's SystemSaveGame.load()
        az_init_system_save_data()
        
        let game = Game(
            path: path,
            title: "Home Menu (\(regionNames[region]))",
            titleId: 0,
            mediaType: Int32(AZ_MEDIA_TYPE_NAND)
        )
        
        appState.currentGame = game
        appState.isEmulating = true
    }
}

/// Helper document for ZipPass export
struct ZipPassDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.zip] }
    
    init() {}
    
    init(configuration: ReadConfiguration) throws {}
    
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        // Create a temporary file for the export
        let tempDir = FileManager.default.temporaryDirectory
        let tempFile = tempDir.appendingPathComponent("streetpass_export.zip")
        
        // Export the zippass data
        let result = az_zippass_export(tempFile.path)
        guard result == 0, FileManager.default.fileExists(atPath: tempFile.path) else {
            throw CocoaError(.fileWriteUnknown)
        }
        
        return try FileWrapper(url: tempFile, options: .immediate)
    }
}

enum SystemArchiveType: String, CaseIterable, Identifiable {
    case sharedFont = "Shared Font"
    case badWordList = "Bad Word List"
    case region = "Region Manifest"
    case homeMenu = "Home Menu"
    case miiMaker = "Mii Maker"
    
    var id: String { rawValue }
    
    var displayName: String { rawValue }
    
    var icon: String {
        switch self {
        case .sharedFont: return "textformat"
        case .badWordList: return "exclamationmark.shield"
        case .region: return "globe"
        case .homeMenu: return "house"
        case .miiMaker: return "person.circle"
        }
    }
    
    var isInstalled: Bool {
        switch self {
        case .sharedFont: return az_shared_font_available()
        case .badWordList: return az_bad_word_list_available()
        case .region: return az_region_manifest_available()
        case .homeMenu: return az_home_menu_available()
        case .miiMaker: return az_mii_maker_available()
        }
    }
}
