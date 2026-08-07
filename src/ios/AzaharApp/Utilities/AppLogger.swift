// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import Foundation
import SwiftUI

/// Comprehensive logging utility that connects to Azahar's C++ logging system
/// All logs go to Documents/Azahar/log/azahar_log.txt
enum AppLogger {
    
    private static var isExperimentalLoggingEnabled: Bool {
        UserDefaults.standard.bool(forKey: "Debugging_experimental_logging")
    }
    
    private static var isExternalDisplayLoggingEnabled: Bool {
        UserDefaults.standard.bool(forKey: "Debugging_external_display_logging")
    }
    
    private static var isGPULoggingEnabled: Bool {
        UserDefaults.standard.bool(forKey: "Debugging_gpu_logging")
    }
    
    private static var is3GXLoggingEnabled: Bool {
        UserDefaults.standard.bool(forKey: "Debugging_3gx_logging")
    }
    
    private static var isControllerLoggingEnabled: Bool {
        UserDefaults.standard.bool(forKey: "Debugging_controller_logging")
    }
    
    /// Log view lifecycle events (onAppear, onDisappear)
    static func viewLifecycle(_ viewName: String, event: String) {
        let message = "[UI] \(viewName): \(event)"
        print(message)
        if isExperimentalLoggingEnabled {
            logToCore(message, level: .info)
        }
    }
    
    /// Log navigation events
    static func navigation(from: String, to: String) {
        let message = "[Navigation] \(from) -> \(to)"
        print(message)
        if isExperimentalLoggingEnabled {
            logToCore(message, level: .info)
        }
    }
    
    /// Log user actions
    static func userAction(_ action: String, details: String = "") {
        let message = details.isEmpty ? "[Action] \(action)" : "[Action] \(action): \(details)"
        print(message)
        if isExperimentalLoggingEnabled {
            logToCore(message, level: .info)
        }
    }
    
    /// Log ROM/game operations
    static func gameOperation(_ operation: String, path: String = "", titleId: UInt64 = 0) {
        let titleStr = titleId != 0 ? String(format: " (TitleID: %016llX)", titleId) : ""
        let pathStr = !path.isEmpty ? " Path: \(path)" : ""
        let message = "[Game] \(operation)\(titleStr)\(pathStr)"
        print(message)
        logToCore(message, level: .info)  // Always log game operations
    }
    
    /// Log errors with full context
    static func error(_ context: String, error: Error) {
        let message = "[Error] \(context): \(error.localizedDescription)"
        print(message)
        logToCore(message, level: .error)  // Always log errors
    }
    
    /// Log errors with custom message
    static func error(_ context: String, message: String) {
        let msg = "[Error] \(context): \(message)"
        print(msg)
        logToCore(msg, level: .error)  // Always log errors
    }
    
    /// Log state changes
    static func stateChange(_ component: String, from: String, to: String) {
        let message = "[State] \(component): \(from) -> \(to)"
        print(message)
        if isExperimentalLoggingEnabled {
            logToCore(message, level: .info)
        }
    }
    
    /// Log generic info (always logged)
    static func info(_ message: String) {
        let msg = "[Info] \(message)"
        print(msg)
        logToCore(msg, level: .info)
    }
    
    /// Log debug info (only with experimental logging)
    static func debug(_ message: String) {
        let msg = "[Debug] \(message)"
        print(msg)
        if isExperimentalLoggingEnabled {
            logToCore(msg, level: .debug)
        }
    }
    
    /// Log critical errors (always logged)
    static func critical(_ context: String, message: String) {
        let msg = "[Critical] \(context): \(message)"
        print(msg)
        logToCore(msg, level: .critical)
    }
    
    /// Log warning (always logged)
    static func warning(_ context: String, message: String) {
        let msg = "[Warning] \(context): \(message)"
        print(msg)
        logToCore(msg, level: .warning)
    }
    
    // MARK: - Category-specific logging
    
    /// External display logging (HDMI/AirPlay)
    static func externalDisplay(_ message: String) {
        let msg = "[ExternalDisplay] \(message)"
        print(msg)
        if isExternalDisplayLoggingEnabled {
            logToCore(msg, level: .info)
        }
    }
    
    /// GPU performance logging
    static func gpu(_ message: String) {
        let msg = "[GPU] \(message)"
        print(msg)
        if isGPULoggingEnabled {
            logToCore(msg, level: .info)
        }
    }
    
    /// 3GX plugin logging
    static func plugin3GX(_ message: String) {
        let msg = "[3GX] \(message)"
        print(msg)
        if is3GXLoggingEnabled {
            logToCore(msg, level: .info)
        }
    }
    
    /// Controller input logging
    static func controller(_ message: String) {
        let msg = "[Controller] \(message)"
        print(msg)
        if isControllerLoggingEnabled {
            logToCore(msg, level: .info)
        }
    }
    
    // MARK: - Private
    
    private enum LogLevel: Int32 {
        case info = 0
        case debug = 1
        case warning = 2
        case error = 3
        case critical = 4
    }
    
    private static func logToCore(_ message: String, level: LogLevel) {
        message.withCString { ptr in
            az_log_message(level.rawValue, ptr)
        }
    }
}

// C bridge function declaration
@_silgen_name("az_log_message")
private func az_log_message(_ level: Int32, _ message: UnsafePointer<CChar>)
