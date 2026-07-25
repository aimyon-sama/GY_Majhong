#ifndef GYMJ_COMMON_ROUND_STATE_HPP
#define GYMJ_COMMON_ROUND_STATE_HPP

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
    std::array<PlayerTileState, 4> states;
    DashChicken one_sou;
    DashChicken eight_pin;
};

struct DashChicken {
    std::optional<Tile> tile;
    int discarded_by = -1;   // 第一张由谁打出
    int claimed_by = -1;     // 没被碰/杠则为 -1
    MeldType claim_type{};   // Pon / OpenKan
};

}

#endif