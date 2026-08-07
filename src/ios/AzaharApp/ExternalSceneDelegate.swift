// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import UIKit
import SwiftUI

/// External display scene delegate for AirPlay/HDMI connections
/// This scene is automatically created by iOS when an external display is connected
/// Based on ManicEMU's implementation
class ExternalSceneDelegate: UIResponder, UIWindowSceneDelegate {
    static var isExternalDisplayActive = false
    var window: UIWindow?
    
    func scene(_ scene: UIScene, willConnectTo session: UISceneSession, options connectionOptions: UIScene.ConnectionOptions) {
        guard let windowScene = scene as? UIWindowScene else { return }
        
        AppLogger.info("[ExternalSceneDelegate] *** EXTERNAL DISPLAY SCENE CONNECTED ***")
        AppLogger.info("[ExternalSceneDelegate] Window scene: \(windowScene)")
        AppLogger.info("[ExternalSceneDelegate] Screen bounds: \(windowScene.screen.bounds)")
        AppLogger.info("[ExternalSceneDelegate] Screen scale: \(windowScene.screen.scale)")
        
        ExternalSceneDelegate.isExternalDisplayActive = true
        
        // Create window for external display
        let window = UIWindow(windowScene: windowScene)
        self.window = window
        
        // Set up external display view controller
        let externalVC = ExternalDisplayViewController()
        window.rootViewController = externalVC
        window.makeKeyAndVisible()
        
        // Notify ExternalDisplayManager
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
        
        // Notify ExternalDisplayManager
        NotificationCenter.default.post(name: Notification.Name("ExternalSceneDisconnected"), object: nil)
    }
    
    func sceneDidBecomeActive(_ scene: UIScene) {
        AppLogger.info("[ExternalSceneDelegate] External scene became active")
    }
    
    func sceneWillResignActive(_ scene: UIScene) {
        AppLogger.info("[ExternalSceneDelegate] External scene will resign active")
    }
}

/// View controller for external display content
class ExternalDisplayViewController: UIViewController {
    private var metalView: UIView?
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        view.backgroundColor = .black
        
        AppLogger.info("[ExternalDisplayViewController] View loaded")
        
        // Add placeholder view (will be replaced with actual Metal rendering surface)
        let label = UILabel()
        label.text = "Azahar External Display\nConnected"
        label.textAlignment = .center
        label.numberOfLines = 0
        label.textColor = .white
        label.font = UIFont.systemFont(ofSize: 24, weight: .bold)
        view.addSubview(label)
        
        label.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            label.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            label.centerYAnchor.constraint(equalTo: view.centerYAnchor)
        ])
    }
    
    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        
        AppLogger.info("[ExternalDisplayViewController] View appeared")
        AppLogger.info("[ExternalDisplayViewController] View bounds: \(view.bounds)")
        
        // Notify that external display is ready
        NotificationCenter.default.post(
            name: Notification.Name("ExternalDisplayViewReady"),
            object: self,
            userInfo: ["view": view]
        )
    }
    
    func setMetalView(_ metalView: UIView) {
        // Remove existing metal view if any
        self.metalView?.removeFromSuperview()
        
        // Add new metal view
        self.metalView = metalView
        metalView.frame = view.bounds
        metalView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.addSubview(metalView)
        view.sendSubviewToBack(metalView)
        
        AppLogger.info("[ExternalDisplayViewController] Metal view set: \(metalView.bounds)")
    }
}
