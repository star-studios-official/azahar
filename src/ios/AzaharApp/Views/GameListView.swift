// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI
import UniformTypeIdentifiers

/// Main game list screen (equivalent to Android's GamesFragment).
struct GameListView: View {
    @EnvironmentObject var appState: AppState
    @State private var searchText = ""
    @State private var showingCIAImport = false
    @State private var isInstallingCIA = false
    @State private var installMessage = ""
    @State private var showInstallResult = false
    @State private var viewMode: ViewMode = .list
    @State private var selectedGameForProperties: Game?
    @State private var showingProperties = false
    @State private var showHomeMenuAlert = false
    @State private var homeMenuMessage = ""
    
    enum ViewMode: String, CaseIterable {
        case list = "List"
        case grid = "Grid"
        
        var icon: String {
            switch self {
            case .list: return "list.bullet"
            case .grid: return "square.grid.2x2"
            }
        }
    }

    var filteredGames: [Game] {
        if searchText.isEmpty { return appState.games }
        return appState.games.filter {
            $0.title.localizedCaseInsensitiveContains(searchText)
        }
    }

    var body: some View {
        Group {
            if viewMode == .grid {
                // Grid view
                GameGridView(
                    games: filteredGames,
                    onSelectGame: { game in
                        appState.launchGame(game)
                    },
                    onShowProperties: { game in
                        selectedGameForProperties = game
                        showingProperties = true
                    }
                )
            } else {
                // List view
                List {
                    // Home Menu boot option
                    Section {
                        Button {
                            let launched = appState.launchHomeMenu()
                            if !launched {
                                homeMenuMessage = "Home Menu is not installed. Download system files first (Settings → System Files → Home Menu)."
                                showHomeMenuAlert = true
                            }
                        } label: {
                            HStack(spacing: 12) {
                                RoundedRectangle(cornerRadius: 8)
                                    .fill(Color.blue.gradient)
                                    .frame(width: 48, height: 48)
                                    .overlay {
                                        Image(systemName: "house.fill")
                                            .foregroundStyle(.white)
                                    }
                                
                                VStack(alignment: .leading, spacing: 4) {
                                    Text("Home Menu")
                                        .font(.headline)
                                    Text("Boot 3DS System Menu")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                                
                                Spacer()
                                
                                Image(systemName: "chevron.right")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                            .padding(.vertical, 4)
                        }
                        .buttonStyle(.plain)
                    } header: {
                        Text("System")
                    }
                    
                    // Games list
                    Section {
                        ForEach(filteredGames) { game in
                            Button {
                                AppLogger.userAction("Selected game", details: game.title)
                                AppLogger.info("Game path: \(game.path)")
                                appState.launchGame(game)
                            } label: {
                                GameRowView(game: game)
                            }
                            .buttonStyle(.plain)
                            .contextMenu {
                                Button {
                                    appState.launchGame(game)
                                } label: {
                                    Label("Play", systemImage: "play.fill")
                                }

                                if game.isGameCardEligible {
                                    Button {
                                        let success = az_insert_cartridge(game.path)
                                        if !success {
                                            AppLogger.error("Game Card", message: "Failed to insert game card")
                                        } else {
                                            AppLogger.info("Game Card: Inserted as game card")
                                        }
                                    } label: {
                                        Label("Load as Game Card", systemImage: "internaldrive.fill")
                                    }
                                }

                                Button {
                                    selectedGameForProperties = game
                                    showingProperties = true
                                } label: {
                                    Label("Properties", systemImage: "info.circle")
                                }
                            }
                            .swipeActions(edge: .trailing, allowsFullSwipe: false) {
                                Button {
                                    selectedGameForProperties = game
                                    showingProperties = true
                                } label: {
                                    Label("Properties", systemImage: "info.circle")
                                }
                                .tint(.blue)
                            }
                        }
                    } header: {
                        if !appState.games.isEmpty {
                            Text("Games")
                        }
                    }
                }
                .overlay {
                    if appState.games.isEmpty {
                        VStack(spacing: 16) {
                            Image(systemName: "gamecontroller")
                                .font(.system(size: 48))
                                .foregroundStyle(.secondary)
                            
                            Text("No games found")
                                .font(.headline)
                                .foregroundStyle(.secondary)
                            
                            VStack(spacing: 8) {
                                Text("To add games:")
                                    .font(.subheadline)
                                    .fontWeight(.medium)
                                
                                Text("1. Tap the + button above")
                                    .font(.caption)
                                Text("2. Select ROM files (.3ds, .cci, .cia, .cxi)")
                                    .font(.caption)
                                Text("3. Files will be imported to Documents/ROMs")
                                    .font(.caption)
                            }
                            .foregroundStyle(.tertiary)
                            .multilineTextAlignment(.center)
                        }
                        .padding()
                    }
                }
            }
        }
        .navigationTitle("Games")
        .searchable(text: $searchText, prompt: "Search games...")
        .refreshable {
            appState.scanGames()
        }
        .toolbar {
            ToolbarItem(placement: .topBarLeading) {
                Menu {
                    Picker("View Mode", selection: $viewMode) {
                        ForEach(ViewMode.allCases, id: \.self) { mode in
                            Label(mode.rawValue, systemImage: mode.icon)
                                .tag(mode)
                        }
                    }
                } label: {
                    Image(systemName: viewMode.icon)
                }
            }
            
            ToolbarItem(placement: .topBarTrailing) {
                Menu {
                    Button {
                        appState.showingDocumentPicker = true
                    } label: {
                        Label("Import ROM", systemImage: "arrow.down.doc")
                    }
                    Button {
                        showingCIAImport = true
                    } label: {
                        Label("Install CIA", systemImage: "shippingbox")
                    }
                } label: {
                    Image(systemName: "plus")
                }
            }
        }
        .sheet(isPresented: $appState.showingDocumentPicker) {
            DocumentPicker(onComplete: { urls in
                for url in urls {
                    appState.importROM(from: url)
                }
                appState.scanGames()
            })
        }
        .fileImporter(
            isPresented: $showingCIAImport,
            allowedContentTypes: [UTType(filenameExtension: "cia") ?? .data],
            allowsMultipleSelection: false
        ) { result in
            handleCIAImport(result)
        }
        .alert("CIA Install", isPresented: $showInstallResult) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(installMessage)
        }
        .overlay {
            if isInstallingCIA {
                ZStack {
                    Color.black.opacity(0.4)
                        .ignoresSafeArea()
                    VStack(spacing: 12) {
                        ProgressView()
                            .scaleEffect(1.5)
                        Text("Installing CIA...")
                            .font(.headline)
                            .foregroundStyle(.white)
                        Text("This may take a moment")
                            .font(.caption)
                            .foregroundStyle(.white.opacity(0.8))
                    }
                }
            }
        }
        .sheet(isPresented: $showingProperties) {
            if let game = selectedGameForProperties {
                GamePropertiesView(game: game)
            }
        }
        .alert("Home Menu Not Installed", isPresented: $showHomeMenuAlert) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(homeMenuMessage)
        }
    }

    private func handleCIAImport(_ result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }

            guard url.startAccessingSecurityScopedResource() else {
                installMessage = "Failed to access the selected file."
                showInstallResult = true
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
                
                isInstallingCIA = true
                DispatchQueue.global(qos: .userInitiated).async {
                    let result = az_install_cia(tempURL.path)
                    
                    // Clean up temp file after installation
                    try? FileManager.default.removeItem(at: tempURL)
                    
                    DispatchQueue.main.async {
                        isInstallingCIA = false
                        installMessage = result == 0
                            ? "CIA installed successfully!"
                            : "Failed to install CIA. Error code: \(result)"
                        showInstallResult = true
                        appState.scanGames()
                    }
                }
            } catch {
                installMessage = "Failed to copy CIA file: \(error.localizedDescription)"
                showInstallResult = true
            }

        case .failure(let error):
            installMessage = "Failed to import file: \(error.localizedDescription)"
            showInstallResult = true
        }
    }
}

struct GameRowView: View {
    let game: Game

    var body: some View {
        HStack(spacing: 12) {
            // Game icon with actual image or placeholder
            if let iconData = game.iconImage, let uiImage = UIImage(data: iconData) {
                Image(uiImage: uiImage)
                    .resizable()
                    .interpolation(.none)
                    .frame(width: 48, height: 48)
                    .cornerRadius(8)
            } else {
                RoundedRectangle(cornerRadius: 8)
                    .fill(Color(.systemGray5))
                    .frame(width: 48, height: 48)
                    .overlay {
                        Image(systemName: "gamecontroller.fill")
                            .foregroundStyle(.secondary)
                    }
            }

            VStack(alignment: .leading, spacing: 4) {
                Text(game.title)
                    .font(.headline)
                    .lineLimit(1)
                
                HStack(spacing: 8) {
                    // Compatibility indicator (colored circle)
                    CompatibilityIndicator(rating: CompatibilityManager.shared.getRating(for: game.titleId))
                    
                    if !game.publisher.isEmpty {
                        Text(game.publisher)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    
                    if game.playTimeSeconds > 0 {
                        Text("•")
                            .font(.caption)
                            .foregroundStyle(.tertiary)
                        Text(game.formattedPlayTime)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            Spacer()
            Image(systemName: "chevron.right")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding(.vertical, 4)
    }
}

/// Compatibility indicator view - colored circle with rating
struct CompatibilityIndicator: View {
    let rating: CompatibilityRating
    
    var body: some View {
        HStack(spacing: 4) {
            Circle()
                .fill(ratingColor)
                .frame(width: 8, height: 8)
            Text(rating.displayName)
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
    }
    
    private var ratingColor: Color {
        switch rating {
        case .unknown: return .gray
        case .wontBoot: return .red
        case .bad: return .red.opacity(0.7)
        case .okay: return .orange
        case .good: return .yellow
        case .great: return .green.opacity(0.7)
        case .excellent: return .green
        }
    }
}
