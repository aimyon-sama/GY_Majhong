#ifndef GYMJ_COMMON_ROUND_STATE_HPP
#define GYMJ_COMMON_ROUND_STATE_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gymj/common/schema/tile.hpp>
#include <gymj/common/player/player_info.hpp>
#include <gymj/common/player/player_action.hpp>
#include <gymj/common/rules/rule_config.hpp>
#include <gymj/common/schema/point.hpp>

namespace gymj::common{

enum class WinType{
    NoWinner,
    Ron,
    Tsumo
};

enum class WinDetail{
    NoWinner,
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
    Tile win_tile = null_tile;
    WinType win_type = WinType::NoWinner;
    WinDetail detail = WinDetail::NoWinner;
    std::array<PlayerTileState, 4> states;
    std::optional<DashChicken> one_sou;
    std::optional<DashChicken> eight_pin;
    // In turn order; winner_seat remains the first winner for single-win callers.
    std::vector<int> winner_seats;
    Tile round_chicken = null_tile;
    std::optional<Tile> chicken_indicator;
};

enum class RoundStage{
    NotActive,
    WaitingDraw,
    WaitingDiscard,
    WaitingClaim,
    Ended
};

enum class DiscardDetail{
    None,
    SimpleDraw,
    AfterPon,
    AfterOpenKanDraw,
    AfterSelfKanDraw,
    AfterAddKanDraw
};

struct RoundState{
    RoundStage stage = RoundStage::NotActive;
    std::array<PlayerTileState, 4> states;
    std::optional<DashChicken> one_sou;
    std::optional<DashChicken> eight_pin;
    int acting_player = -1; // -1 -> no player acting
    std::optional<PlayerAction> pending_action;
    DiscardDetail discard_detail = DiscardDetail::None;
    std::uint64_t seq = 0;
    int tiles_remaining = 0;
    std::array<std::optional<PlayerAction>, 4> claims;
};

enum class RoundEventType{
    RoundStarted,
    InitialHands,
    PlayerDraw,
    PlayerDiscard,
    ClaimSubmitted,
    MeldDeclared,
    AddKanProposed,
    PlayerWin,
    RoundEnded,
    PointsCalculated,
    ChickenRevealed
};

struct RoundConfig {
    int dealer_seat = 0;
    int player_count = 4;
    int action_timeout_ms = 8000;
    std::uint64_t seed = 0;
    RuleConfig rule;
};

// Authoritative events contain hidden tiles; filter them before sending to clients.
struct RoundEvent{
    RoundEventType type = RoundEventType::RoundStarted;
    std::uint64_t seq = 0;
    int player_seat = -1;
    int from_seat = -1;
    std::optional<Tile> tile;
    std::vector<Tile> tiles;
    std::optional<PlayerAction> action;
    DiscardDetail discard_detail = DiscardDetail::None;
};

struct RoundTransition{
    bool accepted = false;
    std::string error;
    std::uint64_t seq_before = 0;
    std::uint64_t seq_after = 0;
    RoundStage stage_before = RoundStage::NotActive;
    RoundStage stage_after = RoundStage::NotActive;
    int actor_before = -1;
    int actor_after = -1;
    std::vector<RoundEvent> events;
    std::array<std::vector<PlayerAction>, 4> available_actions;
    bool round_ended = false;
    std::optional<RoundResult> round_result;
    std::optional<PointResult> point_result;
};

}

#endif
