// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import Foundation

/// Compatibility rating for 3DS games based on compatibility_list
enum CompatibilityRating: Int, Codable {
    case unknown = 0
    case wontBoot = 1        // Red
    case bad = 2             // Dark red
    case okay = 3            // Orange
    case good = 4            // Yellow
    case great = 5           // Light green
    case excellent = 6       // Green
    
    var color: String {
        switch self {
        case .unknown: return "gray"
        case .wontBoot: return "red"
        case .bad: return "darkred"
        case .okay: return "orange"
        case .good: return "yellow"
        case .great: return "lightgreen"
        case .excellent: return "green"
        }
    }
    
    var displayName: String {
        switch self {
        case .unknown: return "Unknown"
        case .wontBoot: return "Won't Boot"
        case .bad: return "Bad"
        case .okay: return "Okay"
        case .good: return "Good"
        case .great: return "Great"
        case .excellent: return "Excellent"
        }
    }
}

/// Manages compatibility ratings from the JSON database
class CompatibilityManager {
    static let shared = CompatibilityManager()
    
    private var compatibilityData: [String: Int] = [:]
    
    private init() {
        loadCompatibilityList()
    }
    
    private func loadCompatibilityList() {
        guard let url = Bundle.main.url(forResource: "compatibility_list", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let jsonArray = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]] else {
            AppLogger.error("CompatibilityManager", message: "Failed to load compatibility_list.json")
            return
        }
        
        // Parse the compatibility list format:
        // [{"compatibility": 99, "releases": [{"id": "000400000008FE00"}], ...}, ...]
        for entry in jsonArray {
            guard let compatibility = entry["compatibility"] as? Int,
                  let releases = entry["releases"] as? [[String: Any]] else {
                continue
            }
            
            // Map compatibility values from database format to our internal ratings:
            // Database: 99 = Perfect, 5 = Great, 4 = Good, 3 = Okay, 2 = Bad, 1 = Won't Boot, 0 = Unknown
            // Our enum: 6 = Excellent, 5 = Great, 4 = Good, 3 = Okay, 2 = Bad, 1 = Won't Boot, 0 = Unknown

            let rating: Int
            switch compatibility {
            case 0: rating = 6  // Perfect → Excellent
            case 1: rating = 5   // Great
            case 2: rating = 4   // Good
            case 3: rating = 3   // Okay
            case 4: rating = 2   // Bad
            case 5: rating = 1   // Won't Boot
            default: rating = 0  // Unknown (includes 0 and any other values)
            }
            
            // Extract all title IDs from releases
            for release in releases {
                if let titleId = release["id"] as? String {
                    compatibilityData[titleId.uppercased()] = rating
                }
            }
        }
        
        AppLogger.info("Loaded \(compatibilityData.count) compatibility entries")
    }
    
    func getRating(for titleId: String) -> CompatibilityRating {
        let normalized = titleId.uppercased().replacingOccurrences(of: "0X", with: "")
        guard let rating = compatibilityData[normalized] else {
            return .unknown
        }
        return CompatibilityRating(rawValue: rating) ?? .unknown
    }
    
    func getRating(for titleId: UInt64) -> CompatibilityRating {
        let hexString = String(format: "%016llX", titleId)
        return getRating(for: hexString)
    }
}
