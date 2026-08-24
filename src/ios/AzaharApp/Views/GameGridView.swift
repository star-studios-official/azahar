// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import SwiftUI

/// Grid layout for game library with icons and metadata
struct GameGridView: View {
    let games: [Game]
    let onSelectGame: (Game) -> Void
    let onShowProperties: (Game) -> Void
    
    private let columns = [
        GridItem(.adaptive(minimum: 140, maximum: 180), spacing: 16)
    ]
    
    var body: some View {
        ScrollView {
            LazyVGrid(columns: columns, spacing: 20) {
                ForEach(games) { game in
                    GameGridItemView(game: game)
                        .onTapGesture {
                            onSelectGame(game)
                        }
                        .contextMenu {
                            Button {
                                onSelectGame(game)
                            } label: {
                                Label("Play", systemImage: "play.fill")
                            }

                            if game.isGameCardEligible {
                                Button {
                                    let success = az_insert_cartridge(game.path)
                                    if !success {
                                        AppLogger.error("Game Card", message: "Failed to insert game card: \(game.title)")
                                    } else {
                                        AppLogger.info("Game Card", details: "Inserted \(game.title) as game card")
                                    }
                                } label: {
                                    Label("Load as Game Card", systemImage: "internaldrive.fill")
                                }
                            }

                            Button {
                                onShowProperties(game)
                            } label: {
                                Label("Properties", systemImage: "info.circle")
                            }
                        }
                }
            }
            .padding()
        }
    }
}

struct GameGridItemView: View {
    let game: Game
    
    var body: some View {
        VStack(spacing: 8) {
            // Game icon (square)
            ZStack {
                if let iconData = game.iconImage, let uiImage = UIImage(data: iconData) {
                    Image(uiImage: uiImage)
                        .resizable()
                        .interpolation(.none) // Preserve pixel art
                        .aspectRatio(contentMode: .fit)
                        .frame(width: 140, height: 140)
                        .background(Color(.systemGray6))
                        .cornerRadius(16)
                        .shadow(radius: 4)
                } else {
                    // Placeholder
                    RoundedRectangle(cornerRadius: 16)
                        .fill(Color(.systemGray5))
                        .frame(width: 140, height: 140)
                        .overlay {
                            Image(systemName: "gamecontroller.fill")
                                .font(.system(size: 40))
                                .foregroundStyle(.secondary)
                        }
                }
                
                // Play time badge (top-right corner)
                if game.playTimeSeconds > 0 {
                    VStack {
                        HStack {
                            Spacer()
                            Text(game.formattedPlayTime)
                                .font(.caption2)
                                .fontWeight(.medium)
                                .foregroundStyle(.white)
                                .padding(.horizontal, 6)
                                .padding(.vertical, 3)
                                .background(Color.blue.opacity(0.9))
                                .cornerRadius(6)
                                .padding(6)
                        }
                        Spacer()
                    }
                }
            }
            .frame(width: 140, height: 140)
            
            // Game title
            Text(game.title)
                .font(.subheadline)
                .fontWeight(.medium)
                .lineLimit(2)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 140)
            
            // Publisher
            if !game.publisher.isEmpty {
                Text(game.publisher)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .frame(maxWidth: 140)
            }
        }
        .frame(width: 160)
    }
}

#Preview {
    GameGridView(
        games: [
            Game(
                path: "/path/to/game1.3ds",
                title: "The Legend of Zelda: Ocarina of Time 3D",
                titleId: 0x0004000000033500,
                mediaType: 0,
                publisher: "Nintendo",
                playTimeSeconds: 3723,
                iconImage: nil
            ),
            Game(
                path: "/path/to/game2.3ds",
                title: "Super Mario 3D Land",
                titleId: 0x0004000000054000,
                mediaType: 0,
                publisher: "Nintendo",
                playTimeSeconds: 0,
                iconImage: nil
            )
        ],
        onSelectGame: { _ in },
        onShowProperties: { _ in }
    )
}
