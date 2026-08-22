// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import UIKit
import SwiftUI

/// Minimal external display scene delegate. External display support is
/// disabled on iOS to avoid Vulkan renderer assertion failures.
class ExternalSceneDelegate: UIResponder, UIWindowSceneDelegate {
    var window: UIWindow?

    func scene(_ scene: UIScene, willConnectTo session: UISceneSession,
               options connectionOptions: UIScene.ConnectionOptions) {
        guard let windowScene = scene as? UIWindowScene else { return }
        // Create a blank window so iOS doesn't log warnings about a nil root.
        let window = UIWindow(windowScene: windowScene)
        window.rootViewController = UIViewController()
        window.isHidden = true
        self.window = window
    }
}
