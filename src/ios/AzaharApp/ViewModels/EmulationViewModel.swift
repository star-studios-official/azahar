// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import Foundation
import SwiftUI

/// Manages the emulation lifecycle on a background thread.
/// (Equivalent to the Android EmulationViewModel + EmulationFragment interaction.)
@MainActor
final class EmulationViewModel: ObservableObject {
    @Published var isRunning = false
    @Published var isPaused = false
    @Published var showPerfStats = false
    @Published var turboEnabled = false
    @Published var gameTitle = ""
    @Published var perfStatsText = ""
    @Published var leftStickPosition: CGPoint = .zero
    @Published var rightStickPosition: CGPoint = .zero
    @Published var isControlsVisible = true
    @Published var isLoading = false
    
    let game: Game

    private var emulationThread: Task<Void, Never>?
    private var perfTimer: Timer?

    init(game: Game) {
        self.game = game
        self.gameTitle = game.title
    }

    func startEmulation() {
        guard !isRunning else { 
            AppLogger.debug("startEmulation() called but already running, ignoring")
            return 
        }
        
        AppLogger.info("=== EMULATION VIEW MODEL START ===")
        AppLogger.gameOperation("Starting emulation", path: game.path, titleId: game.titleId)
        
        isLoading = true
        isRunning = true
        isPaused = false

        AppLogger.debug("isLoading = true, isRunning = true, isPaused = false")

        // Resolve launch path. Empty path means boot to Home Menu.
        let path: String
        if game.path.isEmpty {
            AppLogger.info("Empty game path detected, loading Home Menu")
            let region = Int32(az_setting_get_int("System", "region_value", 0))
            AppLogger.debug("System region: \(region)")
            path = String(cString: az_get_home_menu_path(region))
            AppLogger.info("Home Menu path resolved: \(path)")
            if path.isEmpty {
                AppLogger.error("Home Menu", message: "Home Menu not installed for region \(region)")
                isRunning = false
                isLoading = false
                gameTitle = "Home Menu not installed"
                return
            }
        } else {
            path = game.path
            AppLogger.info("Using game path: \(path)")
        }
        
        // Check if file exists
        let fileExists = FileManager.default.fileExists(atPath: path)
        AppLogger.info("File exists check: \(fileExists)")
        if !fileExists {
            AppLogger.error("ROM Loading", message: "File does not exist at path: \(path)")
        }

        AppLogger.info("Creating emulation thread...")
        emulationThread = Task.detached(priority: .userInitiated) {
            AppLogger.info("Emulation thread started")
            
            // Wait until surface is set.
            AppLogger.debug("Waiting for Metal surface to be ready...")
            var waitCount = 0
            while !az_is_surface_set() {
                // Bail out if emulation was stopped while we were waiting
                if Task.isCancelled {
                    AppLogger.info("Emulation thread cancelled while waiting for surface")
                    return
                }
                waitCount += 1
                if waitCount % 10 == 0 {
                    AppLogger.debug("Still waiting for surface... (\(waitCount * 100)ms)")
                }
                try? await Task.sleep(nanoseconds: 100_000_000) // 100ms
                
                if waitCount > 100 { // 10 second timeout
                    AppLogger.error("Emulation", message: "Timeout waiting for Metal surface!")
                    await MainActor.run {
                        self.isRunning = false
                        self.isLoading = false
                    }
                    return
                }
            }
            AppLogger.info("Metal surface is ready!")

            // Hide loading screen after a brief delay (allows icon to display)
            AppLogger.debug("Waiting 500ms for loading screen display...")
            try? await Task.sleep(nanoseconds: 500_000_000) // 500ms
            await MainActor.run {
                self.isLoading = false
                AppLogger.debug("isLoading = false (loading screen hidden)")
            }

            // One more cancellation check before entering the core
            if Task.isCancelled {
                AppLogger.info("Emulation thread cancelled before entering core")
                return
            }

            AppLogger.info("Calling az_run(\(path))")
            AppLogger.info(">>> ENTERING C++ CORE <<<")
            az_run(path)
            AppLogger.info(">>> RETURNED FROM C++ CORE <<<")

            await MainActor.run {
                self.isRunning = false
                AppLogger.info("Emulation ended, isRunning = false")
            }
        }

        // Start performance stats timer
        perfTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                self?.updatePerfStats()
            }
        }
        
        AppLogger.info("Performance stats timer started")
    }

    func stop() {
        az_stop_emulation()
        emulationThread?.cancel()
        emulationThread = nil
        perfTimer?.invalidate()
        perfTimer = nil
        isRunning = false
        isLoading = false
    }

    func togglePause() {
        if isPaused {
            resume()
        } else {
            pause()
        }
    }

    func pause() {
        az_pause_emulation()
        isPaused = true
    }

    func resume() {
        az_unpause_emulation()
        isPaused = false
    }

    func saveState(slot: Int) {
        az_save_state(Int32(slot))
    }

    func loadState(slot: Int) {
        az_load_state(Int32(slot))
    }

    func cycleLayout() {
        // LayoutOption: 0=Default, 1=SingleScreen, 2=LargeScreen, 3=SideScreen,
        // 4=SeparateWindows (unsafe - needs secondary window), 5=HybridScreen, 6=CustomLayout
        let safeLayouts: [Int32] = [0, 1, 2, 3, 5]  // Skip 4 (SeparateWindows) and 6 (Custom)
        let current = az_setting_get_int("Layout", "layout_option", 2)
        
        // Find next valid layout in the cycle
        var nextIndex = 0
        for (index, layout) in safeLayouts.enumerated() {
            if layout == current {
                nextIndex = (index + 1) % safeLayouts.count
                break
            }
        }
        
        let next = safeLayouts[nextIndex]
        az_setting_set_int("Layout", "layout_option", next)
        az_update_framebuffer(UIScreen.main.bounds.height > UIScreen.main.bounds.width)
    }

    func toggleTurbo() {
        turboEnabled.toggle()
        if turboEnabled {
            az_set_temporary_frame_limit(200)
        } else {
            az_disable_temporary_frame_limit()
        }
    }

    func togglePerfStats() {
        showPerfStats.toggle()
    }
    
    // Additional pause menu functions (matching Android)
    func swapScreens() {
        let current = az_setting_get_bool("Layout", "swap_screen", false)
        az_setting_set_bool("Layout", "swap_screen", !current)
        // az_setting_set_bool already calls az_reload_settings internally
        az_update_framebuffer(UIScreen.main.bounds.height > UIScreen.main.bounds.width)
    }
    
    func toggleEditControls() {
        TouchControlSettings.shared.isEditModeEnabled.toggle()
        TouchControlSettings.shared.save()
    }
    
    func loadAmiibo() {
        // Trigger Amiibo file picker
        // The picker will call loadAmiiboFile() with the selected URL
    }
    
    func loadAmiiboFile(url: URL) {
        guard url.startAccessingSecurityScopedResource() else {
            AppLogger.error("Amiibo", message: "Failed to access file: \(url.path)")
            return
        }
        defer { url.stopAccessingSecurityScopedResource() }
        
        let success = az_load_amiibo(url.path)
        if success {
            AppLogger.info("Amiibo loaded successfully from: \(url.path)")
        } else {
            AppLogger.error("Amiibo", message: "Failed to load Amiibo from: \(url.path)")
        }
    }
    
    func removeAmiibo() {
        az_remove_amiibo()
        AppLogger.info("Amiibo removed")
    }
    
    func takeScreenshot() {
        az_take_screenshot()
    }
    
    func resetGame() {
        az_reset()
    }
    
    func saveStateExists(slot: Int) -> Bool {
        return az_save_state_exists(Int32(slot))
    }
    
    func saveStateTimestamp(slot: Int) -> String? {
        guard saveStateExists(slot: slot) else { return nil }
        
        // Get current title ID
        let titleId = az_get_running_title_id()
        guard titleId > 0 else { return nil }
        
        if let date = SaveStateManager.shared.getTimestamp(for: UInt64(bitPattern: titleId), slot: slot) {
            return SaveStateManager.shared.formatTimestamp(date)
        }
        
        return "Saved"
    }

    private func updatePerfStats() {
        guard isRunning, showPerfStats else { return }
        var stats = [Double](repeating: 0, count: 9)
        az_get_perf_stats(&stats)
        perfStatsText = String(format: "%.0f fps / %.0f%%", stats[1], stats[2] * 100)
    }

    deinit {
        Task { @MainActor [weak self] in
            self?.perfTimer?.invalidate()
        }
    }
}
