#ifndef GYMJ_COMMON_PLAYER_ACTION_HPP
#define GYMJ_COMMON_PLAYER_ACTION_HPP

#include <gymj/common/schema/tile.hpp>

namespace gymj::common{

enum class PlayerActionType{
    None,
    Tsumo,
    Ron,
    Pon,
    Kan,
    Draw,
    Discard
};

struct PlayerAction{
    PlayerActionType type;
    Tile action_tile;
};

}


#endif