#include <gymj/core/rules/rule_engine.hpp>

namespace gymj::rule{

using gymj::common::RoundStage;
using gymj::common::PlayerActionType;
using gymj::common::DiscardDetail;

RuleEngine::RuleEngine(PointRuleConfig point_config, GameRuleConfig game_config)
    :point_engine_(point_config), game_engine_(game_config){}

std::array<std::vector<PlayerAction>, 4> RuleEngine::get_available_actions(const RoundState& state) const{
    std::array<std::vector<PlayerAction>, 4> available_actions{};
    switch (state.stage){
        case RoundStage::NotActive:
        case RoundStage::Ended:
        case RoundStage::WaitingDraw:
            break;
        case RoundStage::WaitingDiscard:{
            int seat = state.acting_player;
            auto& discarder_state = state.states[seat];
            switch(state.discard_detail){
                case DiscardDetail::SimpleDraw:
                case DiscardDetail::AfterAddKanDraw:
                case DiscardDetail::AfterOpenKanDraw:
                case DiscardDetail::AfterSelfKanDraw:{
                    auto drawing_tile = discarder_state.draw_buffer.value();
                    for(auto& t : discarder_state.hand){
                        available_actions[seat].push_back(PlayerAction{PlayerActionType::Discard, t});
                    }
                    if(game_engine_.can_tsumo(discarder_state.hand, discarder_state.melds.size())){
                        available_actions[seat].push_back(PlayerAction{PlayerActionType::Tsumo, drawing_tile});
                    }
                    if(game_engine_.can_add_kan(discarder_state.melds, drawing_tile)){
                        available_actions[seat].push_back(PlayerAction{PlayerActionType::AddKan, drawing_tile});
                    }
                    if(game_engine_.can_self_kan(discarder_state.hand, drawing_tile)){
                        available_actions[seat].push_back(PlayerAction{PlayerActionType::SelfKan, drawing_tile});
                    }
                    if(available_actions[seat].size() > 0){
                        available_actions[seat].push_back(PlayerAction{PlayerActionType::Pass, drawing_tile});
                    }
                    break;
                }
                case DiscardDetail::AfterPon:
                    for(auto& t : discarder_state.hand){
                        available_actions[seat].push_back(PlayerAction{PlayerActionType::Discard, t});
                    }
                    break;
                default:
                    break;
            }
            break;
        }
        case RoundStage::WaitingClaim:{
            int seat = state.acting_player;
            auto action = state.pending_action.value();
            auto tile = action.action_tile;
            bool is_from_add_kan = (action.type == PlayerActionType::AddKan);
            bool is_discard_from_kan = (state.discard_detail == DiscardDetail::AfterSelfKanDraw) ||
                                       (state.discard_detail == DiscardDetail::AfterAddKanDraw) ||
                                       (state.discard_detail == DiscardDetail::AfterOpenKanDraw);
            for(int i = 0; i < 4; i++){
                if(i == seat){
                    continue;
                }
                if(game_engine_.can_pon(state.states[i].hand, tile)){
                    available_actions[i].push_back(PlayerAction{PlayerActionType::Pon, tile});
                }
                if(game_engine_.can_open_kan(state.states[i].hand, tile)){
                    available_actions[i].push_back(PlayerAction{PlayerActionType::OpenKan, tile});
                }
                if(game_engine_.can_ron(state.states[i].hand, state.states[i].melds, tile, (is_from_add_kan || is_discard_from_kan), )){
                    available_actions[i].push_back(PlayerAction{PlayerActionType::Ron, tile});
                }
                if(available_actions[i].size() > 0){
                    available_actions[i].push_back(PlayerAction{PlayerActionType::Pass, tile});
                }
            }
            break;
        }
    }
    return available_actions;
}

std::pair<int, PlayerAction> RuleEngine::resolve_calims(std::array<PlayerAction, 4>& player_claims){
    std::array<int, 4> action_priorities{};
    for(int i = 0; i < 4; i++){
        switch(player_claims[i].type){
            case PlayerActionType::Pass:
                action_priorities[i] = 0;
                break;
            case PlayerActionType::AddKan:
            case PlayerActionType::OpenKan:
            case PlayerActionType::SelfKan:
            case PlayerActionType::Pon:
                action_priorities[i] = 1;
                break;
            case PlayerActionType::Tsumo:
            case PlayerActionType::Ron:
                action_priorities[i] = 2;
                break;
        }
    }
    int highest_priority_seat, temp_max = -1;
    for(int i = 0; i <4; i++){
        if(action_priorities[i] > temp_max){
            highest_priority_seat = i;
            temp_max = action_priorities[i];
        }
    }
    return std::make_pair(highest_priority_seat, player_claims[highest_priority_seat]);
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