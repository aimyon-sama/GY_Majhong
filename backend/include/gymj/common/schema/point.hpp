#ifndef GYMJ_COMMON_POINT_HPP
#define GYMJ_COMMON_POINT_HPP

#include <array>
#include <optional>

#include <gymj/common/schema/tile.hpp>
#include <gymj/common/player/player_info.hpp>

namespace gymj::common{

enum class WinType{
    NoWinner,
    SimpleTsumo,
    TsumoFromKan,
    SimpleRon,
    RonKanDiscard,
    RonAddKan
};

struct RoundResult{
    bool has_winner = 0; // 0 -> no winner
    int winner_seat = -1; // no winner -> -1
    int discarder_seat = -1;// tsumo -> -1
    std::optional<Tile> win_tile;
    WinType win_type;
    std::array<PlayerRoundState, 4> state;
};

struct PointDetail{
    int total_point = 0;
    int point_from_chicken = 0;
    int point_from_kan = 0;
    int point_from_agari = 0;
};

struct PointResult{
    std::array<std::array<int, 4>, 4> point_to_others;
    std::array<int, 4> delta_result;
    std::array<PointDetail, 4> detail;
};

}



#endif