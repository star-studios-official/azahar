// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import UIKit
import SwiftUI

/// External display scene delegate for AirPlay/HDMI connections.
/// iOS automatically creates this scene when an external display connects.
class ExternalSceneDelegate: UIResponder, UIWindowSceneDelegate {
    static var isExternalDisplayActive = false
    var window: UIWindow?

    func scene(_ scene: UIScene, willConnectTo session: UISceneSession, options connectionOptions: UIScene.ConnectionOptions) {
        guard let windowScene = scene as? UIWindowScene else { return }

        AppLogger.info("[ExternalSceneDelegate] *** EXTERNAL DISPLAY SCENE CONNECTED ***")
        AppLogger.info("[ExternalSceneDelegate] Screen bounds: \(windowScene.screen.bounds)")

        ExternalSceneDelegate.isExternalDisplayActive = true

        // Create window for external display.
        let window = UIWindow(windowScene: windowScene)
        self.window = window

        // Host the ExternalScreenView via UIHostingController.
        let hostingController = UIHostingController(
            rootView: ExternalScreenView(displayManager: DisplayManager.shared)
        )
        hostingController.view.backgroundColor = .black
        hostingController.view.frame = windowScene.screen.bounds
        hostingController.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        window.rootViewController = hostingController
        window.makeKeyAndVisible()

        // Notify DisplayManager.
        NotificationCenter.default.post(
            name: Notification.Name("ExternalSceneConnected"),
            object: windowScene.screen,
            userInfo: ["window": window]
        )

        AppLogger.info("[ExternalSceneDelegate] External display window initialized")
    }

    func sceneDidDisconnect(_ scene: UIScene) {
        AppLogger.info("[ExternalSceneDelegate] *** EXTERNAL DISPLAY SCENE DISCONNECTED ***")

        ExternalSceneDelegate.isExternalDisplayActive = false

        window?.isHidden = true
        window = nil

        // Notify DisplayManager.
        NotificationCenter.default.post(name: Notification.Name("ExternalSceneDisconnected"), object: nil)
    }

    func sceneDidBecomeActive(_ scene: UIScene) {
        AppLogger.info("[ExternalSceneDelegate] External scene became active")
    }

    func sceneWillResignActive(_ scene: UIScene) {
        AppLogger.info("[ExternalSceneDelegate] External scene will resign active")
    }
}


