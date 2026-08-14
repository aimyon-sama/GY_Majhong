#include <gymj/core/rules/rule_engine.hpp>

namespace gymj::rule{

using gymj::common::RoundStage;
using gymj::common::PlayerActionType;

RuleEngine::RuleEngine(PointRuleConfig point_config, GameRuleConfig game_config)
    :point_engine_(point_config), game_engine_(game_config){}

std::array<std::vector<PlayerAction>, 4> RuleEngine::get_available_actions(const RoundState& state) const{
    std::array<std::vector<PlayerAction>, 4> available_actions;
    switch (state.stage){
        case RoundStage::NotActive:
        case RoundStage::Ended:
        case RoundStage::WaitingDraw:
            break;
        case RoundStage::WaitingDiscard:
            int seat = state.acting_player;
            for(auto& t : state.states[seat].hand){
                available_actions[seat].push_back(PlayerAction{PlayerActionType::Discard, t});
            }
            break;
        case RoundStage::WaitingClaim:
            break;
    }
    return available_actions;
}

PointResult RuleEngine::calculate_points(const RoundResult& result, const Tile round_chicken){
    std::array<bool, 4> tenpai_seats{};
    for(int i = 0; i < 4; i++){
        int meld_count = result.states[i].melds.size();
        tenpai_seats[i] = game_engine_.is_tenpai(result.states[i].hand, meld_count);
    }
    return point_engine_.calculate(result, round_chicken, tenpai_seats);
}

}