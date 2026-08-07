// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import UIKit
import SwiftUI

/// Main application scene delegate for the iPhone/iPad display
class SceneDelegate: UIResponder, UIWindowSceneDelegate {
    var window: UIWindow?
    
    func scene(_ scene: UIScene, willConnectTo session: UISceneSession, options connectionOptions: UIScene.ConnectionOptions) {
        // Use a UIHostingController as window root view controller
        if let windowScene = scene as? UIWindowScene {
            let window = UIWindow(windowScene: windowScene)
            let appState = AppState.shared
            let contentView = ContentView()
                .environmentObject(appState)
            
            window.rootViewController = UIHostingController(rootView: contentView)
            self.window = window
            window.makeKeyAndVisible()
            
            AppLogger.info("[SceneDelegate] Main scene initialized")
        }
    }
    
    func sceneDidDisconnect(_ scene: UIScene) {
        AppLogger.info("[SceneDelegate] Main scene disconnected")
    }
    
    func sceneDidBecomeActive(_ scene: UIScene) {
        AppLogger.info("[SceneDelegate] Main scene became active")
    }
    
    func sceneWillResignActive(_ scene: UIScene) {
        AppLogger.info("[SceneDelegate] Main scene will resign active")
    }
    
    func sceneWillEnterForeground(_ scene: UIScene) {
        AppLogger.info("[SceneDelegate] Main scene will enter foreground")
    }
    
    func sceneDidEnterBackground(_ scene: UIScene) {
        AppLogger.info("[SceneDelegate] Main scene did enter background")
    }
}
