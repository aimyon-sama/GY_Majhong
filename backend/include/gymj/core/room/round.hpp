#ifndef GYMJ_CORE_ROUND_HPP
#define GYMJ_CORE_ROUND_HPP

#include <vector>
#include <random>

#include <gymj/common/schema/tile.hpp>
#include <gymj/common/schema/round_state.hpp>
#include <gymj/core/rules/rule_engine.hpp>

namespace gymj::room{

using gymj::common::RoundState;
using gymj::common::RoundStage;
using gymj::rule::RuleEngine;
using gymj::common::Tile;
using gymj::common::RoundConfig;
using gymj::common::PlayerInfo;
using gymj::common::RoundTransition;
using gymj::common::PlayerAction;
using gymj::common::PlayerRoundView;
using gymj::common::RoundResult;

class Round{
public:
    Round(RoundConfig config, std::array<PlayerInfo, 4> players, std::mt19937* rng);

    RoundTransition submit_action(int seat, const PlayerAction& action);
    RoundTransition submit_timeout(int seat);
    std::array<std::vector<PlayerAction>, 4> available_actions() const;
    
    const RoundState& state() const noexcept;
    PlayerRoundView view_for(int seat) const;
    bool ended() const noexcept;
    std::optional<RoundResult> result() const;
private:
    std::array<PlayerInfo, 4> players_;

    RoundState state_;
    int cur_player_;
    std::uint64_t seq_num_;// 当前动作id

    std::mt19937* rng_;

    RuleEngine rule_engine_;
    RoundConfig config_;
    RoundResult result_;
};
    
}

#endif