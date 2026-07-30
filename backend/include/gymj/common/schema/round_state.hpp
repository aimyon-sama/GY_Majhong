#ifndef GYMJ_COMMON_ROUND_STATE_HPP
#define GYMJ_COMMON_ROUND_STATE_HPP

#include <array>
#include <optional>

#include <gymj/common/schema/tile.hpp>
#include <gymj/common/player/player_info.hpp>

namespace gymj::common{

enum class WinType{
    NoWinner,
    Ron,
    Tsumo
};

enum class WinDetail{
    Simple,
    TsumoFromKan,
    RonKanDiscard,
    RonAddKan
};

struct DashChicken {
    Tile tile;
    int discarded_by = -1;   // 由谁打出
    int claimed_by = -1;     // 没被碰/杠则为 -1
    MeldType claim_type{};   // Pon / OpenKan
};

struct RoundResult{
    bool has_winner = 0; // 0 -> no winner
    int winner_seat = -1; // no winner -> -1
    int discarder_seat = -1;// tsumo -> -1
    Tile win_tile;
    WinType win_type;
    WinDetail detail;
    std::array<PlayerTileState, 4> states;
    std::optional<DashChicken> one_sou;
    std::optional<DashChicken> eight_pin;
};

}

#endif