#include <gymj/core/room/round.hpp>

#include <optional>
#include <utility>
#include <algorithm>
#include <climits>

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
        wall_.init(rng);
}

RoundTransition Round::start(){
    cur_player_ = config_.dealer_seat;
    state_.stage = RoundStage::WaitingDraw;
    state_.acting_player = cur_player_;
    std::uniform_int_distribution<int> dis(1, INT_MAX >> 1);
    seq_num_ = dis(*rng_);
    generate_initial_hands();
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

Tile Round::draw_a_kan_tile(){
    auto t = wall_.draw_kan_tile();
    return t;
}

Tile Round::draw_a_tile(){
    auto t = wall_.draw_tile();
    return t;
}

void Round::generate_initial_hands(){
    for(int seat = 0; seat < 4; seat++){
        for(int i = 0; i < 4; i++){
            state_.states[seat].hand.push_back(Tile{draw_a_tile()});
        }
    }
    for(int seat = 0; seat < 4; seat++){
        state_.states[seat].hand.push_back(Tile{draw_a_tile()});
    }
}

};