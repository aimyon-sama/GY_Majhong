#ifndef GYMJ_COMMON_PLAYER_INFO_HPP
#define GYMJ_COMMON_PLAYER_INFO_HPP

#include <string>
#include <vector>

#include <gymj/common/schema/tile.hpp>

namespace gymj::common{

struct PlayerInfo{
    std::string player_name;
};

struct PlayerTileState{
    std::vector<Tile> river;
    std::vector<Tile> hand;
    std::vector<Meld> melds;
};

struct PlayerRoundState{
    int winner_count = 0;
    int discarder_count = 0;
    int round_delta = 0;
};
    
}


#endif