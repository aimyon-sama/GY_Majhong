#include <gymj/core/room/round.hpp>

#include <optional>
#include <utility>
#include <algorithm>

namespace gymj::room{

using gymj::common::PlayerTileState;
using gymj::common::DashChicken;
using gymj::common::DiscardDetail;

Round::Round(RoundConfig config, std::array<PlayerInfo, 4> players, std::mt19937* rng) 
    :config_(config), players_(std::move(players)), rule_engine_(config.rule.score, config.rule.game), rng_(rng){
        state_ = RoundState{
            RoundStage::NotActive, 
            std::array<PlayerTileState, 4>(), 
            std::optional<DashChicken>(), 
            std::optional<DashChicken>(), 
            -1,  // -1 -> no player acting
            std::optional<PlayerAction>(), 
            DiscardDetail::None
        };
        cur_player_ = -1;
        seq_num_ = 0;
}

RoundTransition Round::submit_action(int seat, const PlayerAction& action){}
RoundTransition Round::submit_timeout(int seat){}

std::array<std::vector<PlayerAction>, 4> Round::available_actions() const{
    return rule_engine_.get_available_actions(state_);
}

const RoundState& Round::state() const noexcept{
    return state_;
}

PlayerRoundView Round::view_for(int seat) const{
    auto tile_state_view = state_.states;
    for(int i = 0; i < 4; i++){
        if(i == seat){
            continue;
        }
        tile_state_view[i].discard_buffer = std::nullopt;
        tile_state_view[i].draw_buffer = std::nullopt;
        tile_state_view[i].hand = std::vector<Tile>();
    }
    return PlayerRoundView{
        seat,
        seq_num_,
        state_.stage,
        cur_player_,
        tile_state_view,
        available_actions()[seat]
    };
}

bool Round::ended() const noexcept{
    return state_.stage == RoundStage::Ended;
}

std::optional<RoundResult> Round::result() const{
    return ended() ? std::nullopt : std::make_optional(result_);
}



};