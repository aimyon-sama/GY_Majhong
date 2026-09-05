#ifndef GYMJ_COMMON_PLAYER_ACTION_HPP
#define GYMJ_COMMON_PLAYER_ACTION_HPP

#include <gymj/common/schema/tile.hpp>

namespace gymj::common{

enum class PlayerActionType{
    None,
    Tsumo,
    Ron,
    Pon,
    SelfKan,
    AddKan,
    OpenKan,
    Pass,
    Discard
};

struct PlayerAction{
    PlayerActionType type = PlayerActionType::None;
    Tile action_tile;
};

}


#endif