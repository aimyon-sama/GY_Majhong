#ifndef GYMJ_CORE_RULE_ENGINE_HPP
#define GYMJ_CORE_RULE_ENGINE_HPP

#include <vector>
#include <array>

#include <gymj/core/point/point_engine.hpp>
#include <gymj/core/rules/game_engine.hpp>
#include <gymj/common/player/player_action.hpp>
#include <gymj/common/schema/round_state.hpp>

namespace gymj::rule{

using gymj::common::PlayerAction;
using gymj::common::RoundState;
using gymj::common::PointRuleConfig;
using gymj::common::GameRuleConfig;
using gymj::common::PointResult;
using gymj::common::RoundResult;

class RuleEngine{
public:
    RuleEngine(PointRuleConfig point_config, GameRuleConfig game_config);
    std::array<std::vector<PlayerAction>, 4> get_available_actions(const RoundState& state) const;
    PointResult calculate_points(const RoundResult& result, const Tile round_chicken);
private:
    PointEngine point_engine_;
    GameEngine game_engine_;
};

}



#endif