#ifndef GYMJ_COMMON_PLAYER_INFO_HPP
#define GYMJ_COMMON_PLAYER_INFO_HPP

#include <string>
#include <vector>
#include <optional>

#include <gymj/common/schema/tile.hpp>

namespace gymj::common{

struct PlayerInfo{
    std::string player_name;
};

struct PlayerTileState{
    PlayerTileState(){
        river.reserve(20);
        hand.reserve(14);
        melds.reserve(4);
    }
    std::vector<Tile> river;
    std::vector<Tile> hand;
    std::vector<Meld> melds;
    std::optional<Tile> draw_buffer;
    std::optional<Tile> discard_buffer;
};

struct PlayerRoundState{
    int winner_count = 0;
    int discarder_count = 0;
    int round_delta = 0;
};
    
}


#endif