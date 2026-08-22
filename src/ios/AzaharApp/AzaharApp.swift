// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI
import AVFoundation

@main
struct AzaharApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var appState = AppState()
    @StateObject private var jitContext = JITEnableContext.shared
    @Environment(\.scenePhase) private var scenePhase
    @AppStorage("autoEnableJIT") private var autoEnableJIT = false  // Default: disabled
    @AppStorage("hasLaunchedStikDebugThisSession") private var hasLaunchedStikDebug = false
    @State private var showWhatsNew = false

    init() {
        // Install iOS 26 crash prevention handlers
        installJIT26BreakpointHandler()
        
        // Configure AVAudioSession for OpenAL
        do {
            try AVAudioSession.sharedInstance().setCategory(.playback, mode: .default)
            try AVAudioSession.sharedInstance().setActive(true)
        } catch {
            print("Failed to set audio session category: \(error)")
        }
    }

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(appState)
                .environmentObject(jitContext)
                .onAppear {
                    appState.initialize()
                    
                    // Check if What's New should be shown
                    showWhatsNew = WhatsNewManager.shared.shouldShowWhatsNew()
                    
                    // Auto-enable JIT on first launch if configured and not already launched
                    if autoEnableJIT && !jitContext.isJITEnabled && !hasLaunchedStikDebug {
                        hasLaunchedStikDebug = true
                        jitContext.enableJITViaStikDebug()
                    }
                }
                .onChange(of: scenePhase) { oldPhase, newPhase in
                    handleScenePhaseChange(oldPhase: oldPhase, newPhase: newPhase)
                }
                .sheet(isPresented: $showWhatsNew) {
                    if let entry = WhatsNewManager.shared.loadWhatsNew() {
                        WhatsNewView(entry: entry) {
                            WhatsNewManager.shared.markWhatsNewAsSeen()
                            showWhatsNew = false
                        }
                    }
                }
        }
    }
    
    private func handleScenePhaseChange(oldPhase: ScenePhase, newPhase: ScenePhase) {
        switch newPhase {
        case .active:
            // App became active (foreground)
            print("[Lifecycle] App active")
            
            // If StikDebug closed us and we're coming back, reset the flag
            // so user can manually try again if needed
            if oldPhase == .background && jitContext.isJITEnabled {
                // JIT is now enabled, reset for next session
                hasLaunchedStikDebug = false
            }
            
        case .inactive:
            // App becoming inactive (e.g., during transition)
            print("[Lifecycle] App inactive")
            
        case .background:
            // App went to background
            print("[Lifecycle] App background")
            
        @unknown default:
            break
        }
    }
}
/// Top-level observable state for the application.
@MainActor
final class AppState: ObservableObject {
    @Published var games: [Game] = []
    @Published var isEmulating = false
    @Published var currentGame: Game?
    @Published var showingSettings = false
    @Published var showingDocumentPicker = false

    func initialize() {
        let documentsPath = NSSearchPathForDirectoriesInDomains(
            .documentDirectory, .userDomainMask, true
        ).first ?? ""

        // Initialize core
        az_create_log_file()
        az_set_user_directory(documentsPath)
        az_create_config_file()

        // Reset any unsafe layout settings persisted by the old external
        // display code (SeparateWindows crashes without a secondary window).
        // Must run AFTER az_set_user_directory + az_create_config_file so
        // the config file can actually be read.
        DisplayManager.resetLayoutIfNeeded()

        az_init_crypto()  // Initialize AES keys for CIA/NUS operations
        az_init_network()  // Initialize network subsystem for local multiplayer
        az_log_device_info()
        az_play_time_init()

        // Create ROMs directory in Documents
        let romsPath = (documentsPath as NSString).appendingPathComponent("ROMs")
        try? FileManager.default.createDirectory(
            atPath: romsPath,
            withIntermediateDirectories: true
        )

        scanGames()
        
        // Auto-login RetroAchievements if enabled and credentials exist
        if az_ra_is_enabled() {
            let username = String(cString: az_setting_get_string("RetroAchievements", "retro_achievements_username", ""))
            let token = String(cString: az_setting_get_string("RetroAchievements", "retro_achievements_token", ""))
            
            if !username.isEmpty && !token.isEmpty {
                az_ra_login_with_token(username, token)
            }
        }
    }

    func scanGames() {
        games = GameScanner.scan(userDirectory: NSSearchPathForDirectoriesInDomains(
            .documentDirectory, .userDomainMask, true
        ).first ?? "")
    }

    func importROM(from sourceURL: URL) {
        guard sourceURL.startAccessingSecurityScopedResource() else {
            print("Failed to access security scoped resource")
            return
        }
        defer { sourceURL.stopAccessingSecurityScopedResource() }

        let documentsPath = NSSearchPathForDirectoriesInDomains(
            .documentDirectory, .userDomainMask, true
        ).first ?? ""
        let romsPath = (documentsPath as NSString).appendingPathComponent("ROMs")
        let destinationURL = URL(fileURLWithPath: romsPath)
            .appendingPathComponent(sourceURL.lastPathComponent)

        do {
            // Create ROMs directory if it doesn't exist
            try FileManager.default.createDirectory(
                at: URL(fileURLWithPath: romsPath),
                withIntermediateDirectories: true
            )
            
            // Copy the ROM file
            if FileManager.default.fileExists(atPath: destinationURL.path) {
                try FileManager.default.removeItem(at: destinationURL)
            }
            try FileManager.default.copyItem(at: sourceURL, to: destinationURL)
            print("Imported ROM: \(destinationURL.lastPathComponent)")
        } catch {
            print("Failed to import ROM: \(error)")
        }
    }

    func launchGame(_ game: Game) {
        AppLogger.info("=== LAUNCHING GAME ===")
        AppLogger.gameOperation("User tapped game", path: game.path, titleId: game.titleId)
        AppLogger.info("Game title: \(game.title)")
        AppLogger.info("Media type: \(game.mediaType)")
        
        // Verify the file exists before presenting the emulator.
        if !FileManager.default.fileExists(atPath: game.path) {
            AppLogger.error("ROM Loading", message: "Cannot launch \(game.title): file does not exist at \(game.path)")
            return
        }
        
        // If we're already emulating, stop the previous session first so the
        // next launch starts from a clean state. We dispatch the new launch
        // to the next run-loop tick so SwiftUI has time to dismiss the old
        // fullScreenCover before presenting the new one.
        if isEmulating {
            AppLogger.info("Already emulating - stopping previous session")
            az_stop_emulation()
            isEmulating = false
            currentGame = nil
            DispatchQueue.main.async {
                self.beginLaunch(game)
            }
        } else {
            beginLaunch(game)
        }
    }
    
    private func beginLaunch(_ game: Game) {
        AppLogger.stateChange("AppState", from: "idle", to: "launching")
        
        currentGame = game
        isEmulating = true
        
        AppLogger.info("AppState.currentGame set")
        AppLogger.info("AppState.isEmulating = true")
    }
    
    func launchHomeMenu() -> Bool {
        AppLogger.info("=== LAUNCHING HOME MENU ===")
        AppLogger.gameOperation("User launched Home Menu")
        
        // Verify the Home Menu is installed before presenting the emulator.
        if !az_home_menu_available() {
            AppLogger.error("Home Menu", message: "Home Menu is not installed")
            return false
        }
        
        // Initialize system save data (CFG archive, config, etc.)
        // Mirrors Android's SystemSaveGame.load()
        az_init_system_save_data()
        
        // Find the first region that has a Home Menu installed
        var homeMenuPath = ""
        var homeMenuRegion = 1 // Default to USA
        for region in 0..<7 {
            let path = String(cString: az_get_home_menu_path(Int32(region)))
            if !path.isEmpty {
                homeMenuPath = path
                homeMenuRegion = region
                break
            }
        }
        
        guard !homeMenuPath.isEmpty else {
            AppLogger.error("Home Menu", message: "Home Menu path not found for any region")
            return false
        }
        
        // If we're already emulating, stop the previous session first.
        if isEmulating {
            AppLogger.info("Already emulating - stopping previous session")
            az_stop_emulation()
            isEmulating = false
            currentGame = nil
            DispatchQueue.main.async {
                self.beginHomeMenuLaunch(homeMenuPath, homeMenuRegion)
            }
        } else {
            beginHomeMenuLaunch(homeMenuPath, homeMenuRegion)
        }
        
        return true
    }
    
    private func beginHomeMenuLaunch(_ path: String, _ region: Int) {
        let regionNames = ["Japan", "USA", "Europe", "Australia", "China", "Korea", "Taiwan"]
        let regionName = region < regionNames.count ? regionNames[region] : "Unknown"
        
        currentGame = Game(
            path: path,
            title: "Home Menu (\(regionName))",
            titleId: 0x0004003000008F02,
            mediaType: Int32(AZ_MEDIA_TYPE_NAND)
        )
        isEmulating = true
        
        AppLogger.info("Home Menu currentGame created: \(path)")
        AppLogger.info("AppState.isEmulating = true")
    }

    func stopEmulation() {
        AppLogger.info("=== STOPPING EMULATION ===")
        az_stop_emulation()
        isEmulating = false
        currentGame = nil
        AppLogger.stateChange("AppState", from: "emulating", to: "idle")
    }
}
