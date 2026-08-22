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
/// the supported orientations dynamically.
struct EmulationHostView: UIViewControllerRepresentable {
    let game: Game
    @EnvironmentObject var appState: AppState

    func makeUIViewController(context: Context) -> EmulationHostController {
        let emulationView = EmulationView(game: game)
        let controller = EmulationHostController(rootView: emulationView)
        controller.appState = appState
        return controller
    }

    func updateUIViewController(_ uiViewController: EmulationHostController, context: Context) {
        uiViewController.appState = appState
    }
}

/// UIHostingController subclass for emulation.
final class EmulationHostController: UIHostingController<EmulationView> {
    var appState: AppState?

    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .all }
    override var shouldAutorotate: Bool { true }
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
