#include <gymj/core/room/round.hpp>

#include <optional>
#include <utility>
#include <algorithm>
#include <climits>

namespace gymj::room{

using gymj::common::PlayerTileState;
using gymj::common::DashChicken;
using gymj::common::DiscardDetail;
using gymj::common::RoundEvent;
using gymj::common::RoundEventType;
using gymj::common::PlayerActionType;
using gymj::common::null_tile;

namespace{

RoundTransition make_rejected_transition(
    std::uint64_t seq,
    RoundStage stage,
    int actor,
    std::array<std::vector<PlayerAction>, 4> actions,
    std::string error
){
    RoundTransition transition{};
    transition.accepted = false;
    transition.error = std::move(error);
    transition.seq_before = seq;
    transition.seq_after = seq;
    transition.stage_before = stage;
    transition.stage_after = stage;
    transition.actor_before = actor;
    transition.actor_after = actor;
    transition.available_actions = std::move(actions);
    transition.round_ended = (stage == RoundStage::Ended);
    return transition;
}

}

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

    RoundTransition transition{};
    transition.seq_before = 0;
    transition.seq_after = seq_num_;
    transition.stage_before = RoundStage::NotActive;
    transition.stage_after = state_.stage;
    transition.actor_before = -1;
    transition.actor_after = cur_player_;
    transition.available_actions = {};
    transition.round_ended = false;
    transition.events.push_back(RoundEvent{
        seq_num_,
        RoundEventType::RoundStarted,
        cur_player_,
        -1,
        std::nullopt,
        {}
    });
    for(int seat = 0; seat < 4; ++seat){
        transition.events.push_back(RoundEvent{
            seq_num_,
            RoundEventType::InitialHands,
            seat,
            -1,
            std::nullopt,
            state_.states[seat].hand
        });
    }
    return transition;
}

RoundTransition Round::submit_action(int seat, const PlayerAction& action){
    if(seat < 0 || seat >= config_.player_count){
        return make_rejected_transition(
            seq_num_,
            state_.stage,
            cur_player_,
            available_actions(),
            "invalid seat"
        );
    }
    switch(action.type){
        case PlayerActionType::Discard:
            return make_rejected_transition(
                seq_num_,
                state_.stage,
                cur_player_,
                available_actions(),
                "discard is not implemented yet"
            );
        case PlayerActionType::Tsumo:
            return make_rejected_transition(
                seq_num_,
                state_.stage,
                cur_player_,
                available_actions(),
                "tsumo is not implemented yet"
            );
        case PlayerActionType::Ron:
            return make_rejected_transition(
                seq_num_,
                state_.stage,
                cur_player_,
                available_actions(),
                "ron is not implemented yet"
            );
        case PlayerActionType::Pon:
            return make_rejected_transition(
                seq_num_,
                state_.stage,
                cur_player_,
                available_actions(),
                "pon is not implemented yet"
            );
        case PlayerActionType::SelfKan:
            return make_rejected_transition(
                seq_num_,
                state_.stage,
                cur_player_,
                available_actions(),
                "self kan is not implemented yet"
            );
        case PlayerActionType::AddKan:
            return make_rejected_transition(
                seq_num_,
                state_.stage,
                cur_player_,
                available_actions(),
                "add kan is not implemented yet"
            );
        case PlayerActionType::OpenKan:
            return make_rejected_transition(
                seq_num_,
                state_.stage,
                cur_player_,
                available_actions(),
                "open kan is not implemented yet"
            );
        case PlayerActionType::Pass:
            return make_rejected_transition(
                seq_num_,
                state_.stage,
                cur_player_,
                available_actions(),
                "pass is not implemented yet"
            );
        default:
            return make_rejected_transition(
                seq_num_,
                state_.stage,
                cur_player_,
                available_actions(),
                "invalid action"
            );
    }
}

RoundTransition Round::draw_for_current_player(){
    if(state_.stage != RoundStage::WaitingDraw || state_.acting_player != cur_player_){
        return make_rejected_transition(
            seq_num_,
            state_.stage,
            state_.acting_player,
            {},
            "can not draw"
        );
    }
    RoundTransition transition{};
    transition.seq_before = seq_num_;
    transition.stage_before = state_.stage;
    transition.actor_before = cur_player_;
    if(wall_.empty()){
        cur_player_ = -1;
        state_.stage = RoundStage::Ended;
        state_.acting_player = -1;
        state_.pending_action = std::nullopt;
        state_.discard_detail = DiscardDetail::None;
        ++seq_num_;
        auto round_draw_event = RoundEvent{
            seq_num_,
            RoundEventType::RoundDraw,
            -1,
            -1,
            std::nullopt,
            {}
        };
        transition.events.push_back(round_draw_event);
        auto result = RoundResult{
            false,
            -1,
            -1,
            null_tile,
            gymj::common::WinType::NoWinner,
            gymj::common::WinDetail::NoWinner,
            state_.states,
            state_.one_sou,
            state_.eight_pin
        };
        transition.round_result = result;
        transition.point_result = rule_engine_.calculate_points(result, null_tile);
        transition.events.push_back(RoundEvent{
            seq_num_,
            RoundEventType::PointCalculated,
            -1,
            -1,
            std::nullopt,
            {}
        });
        transition.events.push_back(RoundEvent{
            seq_num_,
            RoundEventType::RoundEnded,
            -1,
            -1,
            std::nullopt,
            {}
        });
        transition.stage_after = RoundStage::Ended;
        transition.actor_after = -1;
        transition.available_actions = {};
        transition.round_ended = true;

        result_ = result;
    } else {
        auto t = draw_a_tile();
        state_.stage = RoundStage::WaitingDiscard;
        state_.states[cur_player_].draw_buffer = t;
        state_.states[cur_player_].hand.push_back(t);
        state_.discard_detail = DiscardDetail::SimpleDraw;
        state_.pending_action = std::nullopt;
        state_.acting_player = cur_player_;

        ++seq_num_;

        auto event = RoundEvent{
            seq_num_,
            RoundEventType::PlayerDraw,
            cur_player_,
            -1,
            std::make_optional(t),
            {}
        };

        transition.events.push_back(event);
        transition.stage_after = RoundStage::WaitingDiscard;
        transition.actor_after = cur_player_;
        transition.available_actions = available_actions();
        transition.round_ended = false;
    }
    transition.seq_after = seq_num_;
    return transition;
}

RoundTransition Round::submit_timeout(int seat){
    if(seat < 0 || seat >= config_.player_count){
        return make_rejected_transition(
            seq_num_,
            state_.stage,
            cur_player_,
            available_actions(),
            "invalid seat"
        );
    }
    return make_rejected_transition(
        seq_num_,
        state_.stage,
        cur_player_,
        available_actions(),
        "submit_timeout is not implemented yet"
    );
}

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
    return ended() ? std::make_optional(result_) : std::nullopt;
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
    for(int times = 0; times < 3; times++){
        for(int seat = 0; seat < 4; seat++){
            for(int i = 0; i < 4; i++){
                state_.states[seat].hand.push_back(Tile{draw_a_tile()});
            }
        }
    }
    for(int seat = 0; seat < 4; seat++){
        state_.states[seat].hand.push_back(Tile{draw_a_tile()});
    }
}

};
