// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI
import UniformTypeIdentifiers

/// Root navigation view (equivalent to Android's MainActivity).
struct RootView: View {
    @EnvironmentObject var appState: AppState

    var body: some View {
        NavigationStack {
            GameListView()
                .toolbar {
                    ToolbarItem(placement: .topBarLeading) {
                        Button {
                            AppLogger.userAction("Opened Settings")
                            appState.showingSettings = true
                        } label: {
                            Label("Settings", systemImage: "gear")
                        }
                    }
                }
                .sheet(isPresented: $appState.showingSettings) {
                    SettingsView()
                }
                .fullScreenCover(isPresented: $appState.isEmulating) {
                    if let game = appState.currentGame {
                        EmulationHostView(game: game)
                            .onAppear {
                                AppLogger.viewLifecycle("RootView", event: "presenting EmulationView fullScreenCover")
                            }
                    }
                }
        }
        .onAppear {
            AppLogger.viewLifecycle("RootView", event: "appeared")
        }
    }
}

/// Hosts the EmulationView in a custom UIViewController so we can control
/// the supported orientations dynamically (e.g. force landscape when the
/// top screen is on an external display and the bottom screen is on the iPhone).
struct EmulationHostView: UIViewControllerRepresentable {
    let game: Game
    @EnvironmentObject var appState: AppState

    func makeUIViewController(context: Context) -> EmulationHostController {
        // Pass the game to the EmulationView; the environment object is
        // inherited from the SwiftUI hierarchy
        let emulationView = EmulationView(game: game)
        let controller = EmulationHostController(rootView: emulationView)
        controller.appState = appState
        return controller
    }

    func updateUIViewController(_ uiViewController: EmulationHostController, context: Context) {
        // Update if needed
        uiViewController.appState = appState
    }
}

/// UIHostingController subclass that enforces orientation based on the external
/// display mode. In topScreenExternal mode the iPhone is forced into landscape
/// so the bottom screen + touch controls are shown correctly.
final class EmulationHostController: UIHostingController<EmulationView> {
    var appState: AppState?
    private var modeObserver: NSObjectProtocol?

    override var supportedInterfaceOrientations: UIInterfaceOrientationMask {
        let manager = DisplayManager.shared
        if manager.isExternalDisplayConnected && manager.displayMode == .externalTopScreen {
            return .landscape
        }
        return .all
    }

    override var shouldAutorotate: Bool { true }

    override func viewDidLoad() {
        super.viewDidLoad()
        // Re-evaluate orientation whenever the display mode changes
        modeObserver = NotificationCenter.default.addObserver(
            forName: Notification.Name("ExternalDisplayModeChanged"),
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.setNeedsUpdateOfSupportedInterfaceOrientations()
        }
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        setNeedsUpdateOfSupportedInterfaceOrientations()
    }

    deinit {
        if let observer = modeObserver {
            NotificationCenter.default.removeObserver(observer)
        }
    }
}

/// Document picker for importing ROMs
struct DocumentPicker: UIViewControllerRepresentable {
    let onComplete: ([URL]) -> Void
    
    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let supportedTypes: [UTType] = [
            UTType(filenameExtension: "3ds") ?? .data,
            UTType(filenameExtension: "cci") ?? .data,
            UTType(filenameExtension: "cxi") ?? .data,
            UTType(filenameExtension: "3dsx") ?? .data,
            UTType(filenameExtension: "cia") ?? .data,
            UTType(filenameExtension: "z3ds") ?? .data,
            UTType(filenameExtension: "zcci") ?? .data,
            UTType(filenameExtension: "zcxi") ?? .data,
            UTType(filenameExtension: "z3dsx") ?? .data,
            UTType(filenameExtension: "zcia") ?? .data,
            UTType(filenameExtension: "elf") ?? .data,
            UTType(filenameExtension: "axf") ?? .data,
        ]
        
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: supportedTypes)
        picker.allowsMultipleSelection = true
        picker.delegate = context.coordinator
        return picker
    }
    
    func updateUIViewController(_ uiViewController: UIDocumentPickerViewController, context: Context) {}
    
    func makeCoordinator() -> Coordinator {
        Coordinator(onComplete: onComplete)
    }
    
    class Coordinator: NSObject, UIDocumentPickerDelegate {
        let onComplete: ([URL]) -> Void
        
        init(onComplete: @escaping ([URL]) -> Void) {
            self.onComplete = onComplete
        }
        
        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
            onComplete(urls)
        }
    }
}
