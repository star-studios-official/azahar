// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI

/// Cheats editor (equivalent to Android's CheatsActivity/CheatListFragment).
struct CheatsView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var cheats: [CheatEntry] = []

    struct CheatEntry: Identifiable {
        let id: Int64
        let name: String
        let notes: String
        var enabled: Bool
    }

    var body: some View {
        List {
            if cheats.isEmpty {
                ContentUnavailableView(
                    "No Cheats Found",
                    systemImage: "questionmark.diamond",
                    description: Text("Load a game first, then cheats will appear here.")
                )
            } else {
                ForEach($cheats) { $cheat in
                    Toggle(isOn: Binding(
                        get: { cheat.enabled },
                        set: { newValue in
                            cheat.enabled = newValue
                            _ = az_cheats_set_enabled(cheat.id, newValue)
                        }
                    )) {
                        VStack(alignment: .leading) {
                            Text(cheat.name).font(.headline)
                            if !cheat.notes.isEmpty {
                                Text(cheat.notes)
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                }
            }
        }
        .navigationTitle("Cheats")
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                Button("Apply") {
                    az_cheats_apply()
                    dismiss()
                }
            }
        }
        .onAppear {
            loadCheats()
        }
    }

    private func loadCheats() {
        let titleId = az_get_running_title_id()
        guard titleId != 0 else {
            AppLogger.warning("Cheats", message: "No game running (titleId == 0)")
            return
        }
        
        var cEntries = [az_cheat_entry](repeating: az_cheat_entry(), count: 256)
        let count = az_cheats_load(nil, &cEntries, Int32(cEntries.count))
        
        guard count > 0 else {
            AppLogger.info("Cheats: No cheats found for title \(String(format: "%016X", titleId))")
            return
        }
        
        cheats = (0..<Int(count)).map { i in
            let e = cEntries[i]
            return CheatEntry(
                id: e.cheat_id,
                name: e.name.map { String(cString: $0) } ?? "Unknown",
                notes: e.notes.map { String(cString: $0) } ?? "",
                enabled: e.enabled
            )
        }
        AppLogger.info("Cheats: Loaded \(cheats.count) cheats")
    }
}
