// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import Foundation
import Security

typealias SecTaskRef = OpaquePointer

@_silgen_name("SecTaskCopyValueForEntitlement")
func SecTaskCopyValueForEntitlement(
    _ task: SecTaskRef,
    _ entitlement: NSString,
    _ error: NSErrorPointer
) -> CFTypeRef?

@_silgen_name("SecTaskCreateFromSelf")
func SecTaskCreateFromSelf(
    _ allocator: CFAllocator?
) -> SecTaskRef?

@_silgen_name("CFRelease")
func CFRelease(_ cf: CFTypeRef?)

func releaseSecTask(_ task: SecTaskRef) {
    let cf = unsafeBitCast(task, to: CFTypeRef.self)
    CFRelease(cf)
}

/// Raw runtime value of an entitlement granted to the current process, or nil if the
/// entitlement is not present in the process's active code signature.
func runtimeEntitlementValue(_ entitlement: String) -> CFTypeRef? {
    guard let task = SecTaskCreateFromSelf(nil) else {
        return nil
    }
    defer {
        releaseSecTask(task)
    }
    return SecTaskCopyValueForEntitlement(task, entitlement as NSString, nil)
}

/// Check if the app has a specific entitlement granted at runtime.
func checkAppEntitlement(_ entitlement: String) -> Bool {
    guard let value = runtimeEntitlementValue(entitlement) else {
        return false
    }

    if let number = value as? NSNumber {
        return number.boolValue
    } else if let bool = value as? Bool {
        return bool
    }

    // Non-boolean entitlements (arrays, strings) count as present when non-empty.
    return true
}

/// Entitlements the app declares in its bundle: the Azahar.entitlements file copied next to the
/// app (the CI does this) or the entitlements plist the signing tool embedded in _CodeSignature/.
/// Returns a dictionary of entitlement key -> plist value, or an empty dict when neither is found.
func declaredEntitlements() -> [String: Any] {
    let candidates: [URL] = [
        Bundle.main.url(forResource: "Azahar", withExtension: "entitlements"),
        Bundle.main.url(forResource: "entitlements", withExtension: "plist",
                        subdirectory: "_CodeSignature"),
    ].compactMap { $0 }

    for url in candidates {
        guard let data = try? Data(contentsOf: url),
              let plist = try? PropertyListSerialization.propertyList(from: data, options: [],
                                                                      format: nil),
              let dict = plist as? [String: Any] else {
            continue
        }
        return dict
    }
    return [:]
}
