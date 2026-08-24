// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import Foundation
import UIKit

/// Represents a game discovered in the user directory.
struct Game: Identifiable, Hashable {
    let id = UUID()
    let path: String
    let title: String
    let titleId: UInt64
    let mediaType: Int32
    var publisher: String = ""
    var playTimeSeconds: Int64 = 0
    var iconImage: Data? = nil

    var formattedTitleId: String {
        String(format: "%016llX", titleId)
    }
    
    var formattedPlayTime: String {
        guard playTimeSeconds > 0 else { return "Not played" }
        
        let hours = playTimeSeconds / 3600
        let minutes = (playTimeSeconds % 3600) / 60
        
        if hours > 0 {
            return "\(hours)h \(minutes)m"
        } else if minutes > 0 {
            return "\(minutes)m"
        } else {
            return "<1m"
        }
    }

    /// Whether this game can be inserted as a virtual game card (.3ds / .cci / .nds)
    var isGameCardEligible: Bool {
        let ext = (path as NSString).pathExtension.lowercased()
        return ext == "3ds" || ext == "cci" || ext == "z3ds" || ext == "zcci" ||
            ext == "nds" || ext == "dsi"
    }
}

/// Scans the user directory for game files.
enum GameScanner {
    private static let supportedExtensions: Set<String> = [
        "3ds", "3dsx", "cxi", "app", "cia", "ncch", "cci", "z3ds", "zcci", "zcxi",
        // TWL (DS/DSi) and AGB (GBA) ROMs; playback requires the TWL_FIRM/AGB_FIRM
        // subsystems, but the files are recognized and can be inserted as game cards.
        "nds", "dsi", "gba"
    ]

    static func scan(userDirectory: String) -> [Game] {
        var games: [Game] = []

        // The emulator stores data under Documents/Azahar/, while
        // userDirectory is the raw Documents path.  Build paths that
        // match the actual layout on disk.
        let azaharDir = (userDirectory as NSString).appendingPathComponent("Azahar")

        // Use the bridge to enumerate installed titles (CIA installs
        // to SDMC via AM).  This uses the same FileUtil::GetUserPath
        // logic as the C++ core, so paths are always correct.
        let maxInstalled = 512
        var installedPaths = [az_game_path](repeating: az_game_path(), count: maxInstalled)
        let installedCount = az_get_installed_game_paths(&installedPaths, Int32(maxInstalled))
        for i in 0..<Int(installedCount) {
            let cPath = installedPaths[i].path
            guard let path = cPath else { continue }
            let fullPath = String(cString: path)
            let mediaType = Int32(installedPaths[i].media_type)
            // Extract metadata via bridge
            var metadata = az_game_metadata()
            let hasMetadata = az_get_game_metadata(fullPath, &metadata)

            let title: String
            let publisher: String
            let playTime: Int64
            let titleId: UInt64

            if hasMetadata {
                title = withUnsafePointer(to: &metadata.title) { ptr in
                    ptr.withMemoryRebound(to: CChar.self, capacity: 256) {
                        String(cString: $0)
                    }
                }
                publisher = withUnsafePointer(to: &metadata.publisher) { ptr in
                    ptr.withMemoryRebound(to: CChar.self, capacity: 256) {
                        String(cString: $0)
                    }
                }
                playTime = metadata.play_time_seconds
                titleId = metadata.title_id
            } else {
                title = (fullPath as NSString).lastPathComponent
                publisher = ""
                playTime = 0
                let tid = az_get_title_id(fullPath)
                titleId = UInt64(bitPattern: tid)
            }

            // Extract icon
            let iconSize = 48 * 48
            var iconData = [UInt16](repeating: 0, count: iconSize)
            let pixelCount = az_get_game_icon(fullPath, &iconData, Int32(iconSize))

            var iconImageData: Data? = nil
            if pixelCount == iconSize {
                iconImageData = createPNGFromRGB565(iconData, width: 48, height: 48)
            }

            games.append(Game(
                path: fullPath,
                title: title,
                titleId: titleId,
                mediaType: mediaType,
                publisher: publisher,
                playTimeSeconds: playTime,
                iconImage: iconImageData
            ))
        }



        // Scan NAND titles (Home Menu, system applets, etc.)
        let nandTitles = (azaharDir as NSString).appendingPathComponent(
            "nand/00000000000000000000000000000000/title/00040000"
        )
        games.append(contentsOf: scanDirectory(nandTitles, mediaType: Int32(AZ_MEDIA_TYPE_NAND)))

        // Scan ROMs directory (recursively) - where imported ROMs are stored
        let docsDir = NSSearchPathForDirectoriesInDomains(
            .documentDirectory, .userDomainMask, true
        ).first ?? ""

        let gamesPath = (docsDir as NSString).appendingPathComponent("ROMs")
        games.append(contentsOf: scanDirectory(gamesPath, mediaType: Int32(AZ_MEDIA_TYPE_SDMC)))

        // Deduplicate by path
        var seen = Set<String>()
        return games.filter { seen.insert($0.path).inserted }
    }

    private static func scanDirectory(_ path: String, mediaType: Int32) -> [Game] {
        guard let enumerator = FileManager.default.enumerator(
            at: URL(fileURLWithPath: path),
            includingPropertiesForKeys: [.isRegularFileKey],
            options: [.skipsHiddenFiles]
        ) else { return [] }

        var games: [Game] = []
        for case let fileURL as URL in enumerator {
            let ext = fileURL.pathExtension.lowercased()
            guard supportedExtensions.contains(ext) else { continue }
            let fullPath = fileURL.path
            
            // Extract metadata via bridge
            var metadata = az_game_metadata()
            let hasMetadata = az_get_game_metadata(fullPath, &metadata)
            
            let title: String
            let publisher: String
            let playTime: Int64
            let titleId: UInt64
            
            if hasMetadata {
                // Convert C char arrays to Swift strings
                title = withUnsafePointer(to: &metadata.title) { ptr in
                    ptr.withMemoryRebound(to: CChar.self, capacity: 256) {
                        String(cString: $0)
                    }
                }
                publisher = withUnsafePointer(to: &metadata.publisher) { ptr in
                    ptr.withMemoryRebound(to: CChar.self, capacity: 256) {
                        String(cString: $0)
                    }
                }
                playTime = metadata.play_time_seconds
                titleId = metadata.title_id
            } else {
                title = fileURL.deletingPathExtension().lastPathComponent
                publisher = ""
                playTime = 0
                let tid = az_get_title_id(fullPath)
                titleId = UInt64(bitPattern: tid)
            }
            
            // Extract icon
            let iconSize = 48 * 48
            var iconData = [UInt16](repeating: 0, count: iconSize)
            let pixelCount = az_get_game_icon(fullPath, &iconData, Int32(iconSize))
            
            var iconImageData: Data? = nil
            if pixelCount == iconSize {
                // Convert RGB565 to PNG data
                iconImageData = createPNGFromRGB565(iconData, width: 48, height: 48)
            }
            
            games.append(Game(
                path: fullPath,
                title: title,
                titleId: titleId,
                mediaType: mediaType,
                publisher: publisher,
                playTimeSeconds: playTime,
                iconImage: iconImageData
            ))
        }
        return games
    }
    
    /// Converts RGB565 pixel data to PNG Data
    private static func createPNGFromRGB565(_ pixels: [UInt16], width: Int, height: Int) -> Data? {
        // Create RGBA8888 buffer
        var rgbaPixels = [UInt8](repeating: 0, count: width * height * 4)
        
        for i in 0..<pixels.count {
            let rgb565 = pixels[i]
            
            // Extract RGB components from RGB565
            let r5 = UInt8((rgb565 >> 11) & 0x1F)
            let g6 = UInt8((rgb565 >> 5) & 0x3F)
            let b5 = UInt8(rgb565 & 0x1F)
            
            // Convert to 8-bit (scale up)
            let r8 = (r5 << 3) | (r5 >> 2)
            let g8 = (g6 << 2) | (g6 >> 4)
            let b8 = (b5 << 3) | (b5 >> 2)
            
            // Write RGBA pixel
            let offset = i * 4
            rgbaPixels[offset + 0] = r8
            rgbaPixels[offset + 1] = g8
            rgbaPixels[offset + 2] = b8
            rgbaPixels[offset + 3] = 255 // Alpha
        }
        
        // Create CGImage
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        let bitmapInfo = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue)
        
        guard let dataProvider = CGDataProvider(data: Data(rgbaPixels) as CFData),
              let cgImage = CGImage(
                width: width,
                height: height,
                bitsPerComponent: 8,
                bitsPerPixel: 32,
                bytesPerRow: width * 4,
                space: colorSpace,
                bitmapInfo: bitmapInfo,
                provider: dataProvider,
                decode: nil,
                shouldInterpolate: false,
                intent: .defaultIntent
              ) else {
            return nil
        }
        
        // Convert to PNG data
        let uiImage = UIImage(cgImage: cgImage)
        return uiImage.pngData()
    }
}
