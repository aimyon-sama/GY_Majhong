#ifndef GYMJ_COMMON_TILE_HPP
#define GYMJ_COMMON_TILE_HPP

#include <string>
#include <cstdint>

namespace gymj::common{

enum class TileType{
    Man,
    Sou,
    Pin
};

struct Tile{
    TileType type;
    std::uint8_t rank;
};

enum class MeldType{
    Pon,
    OpenKan,
    SelfKan,
    AddKan
};

struct Meld{
    MeldType type;
    Tile tile;
    std::uint8_t from_seat;
};

inline std::string tile_to_string(Tile t){
    char type;
    switch(t.type){
        case TileType::Man:
            type = 'm';
            break;
        case TileType::Sou:
            type = 's';
            break;
        case TileType::Pin:
            type = 'p';
            break;
        default:
            type = char();
    }
    return std::to_string(t.rank) + type;
}

inline bool operator ==(const Tile& lhs, const Tile& rhs) noexcept{
    return lhs.rank == rhs.rank && lhs.type == rhs.type;
}

constexpr int suitCount = 3;
constexpr int ranksPerSuit = 9;
constexpr int tileKindCount = suitCount * ranksPerSuit;
constexpr int handTileCount = 14;
constexpr int meldTileCount = 3;
constexpr int completeMeldCount = 4;

inline int tile_index(Tile tile){
    if(tile.rank < 1 || tile.rank > 9){
        return -1;
    }

    int suit_offset = 0;
    switch(tile.type){
        case gymj::common::TileType::Man:
            suit_offset = 0;
            break;
        case gymj::common::TileType::Sou:
            suit_offset = ranksPerSuit;
            break;
        case gymj::common::TileType::Pin:
            suit_offset = ranksPerSuit * 2;
            break;
        default:
            return -1;
    }

    return suit_offset + static_cast<int>(tile.rank) - 1;
}

inline Tile tile_from_index(int index){
    const auto rank = static_cast<std::uint8_t>(index % ranksPerSuit + 1);
    if(index < ranksPerSuit){
        return Tile{gymj::common::TileType::Man, rank};
    }
    if(index < ranksPerSuit * 2){
        return Tile{gymj::common::TileType::Sou, rank};
    }
    return Tile{gymj::common::TileType::Pin, rank};
}

}



#endif