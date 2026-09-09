#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <gymj/core/room/round.hpp>

namespace{

using gymj::common::DiscardDetail;
using gymj::common::PlayerActionType;
using gymj::common::PlayerInfo;
using gymj::common::RoundConfig;
using gymj::common::RoundEventType;
using gymj::common::RoundStage;
using gymj::common::Tile;
using gymj::room::Round;

void require(bool condition, const std::string& message){
    if(!condition){
        throw std::runtime_error(message);
    }
}

std::array<PlayerInfo, 4> players(){
    return {
        PlayerInfo{"east"},
        PlayerInfo{"south"},
        PlayerInfo{"west"},
        PlayerInfo{"north"},
    };
}

RoundConfig config_with_dealer(int dealer_seat){
    RoundConfig config{};
    config.dealer_seat = dealer_seat;
    return config;
}

int discard_action_count(const gymj::common::RoundTransition& transition, int seat){
    int count = 0;
    for(const auto& action : transition.available_actions[seat]){
        if(action.type == PlayerActionType::Discard){
            ++count;
        }
    }
    return count;
}

void require_same_actions(
    const std::array<std::vector<gymj::common::PlayerAction>, 4>& lhs,
    const std::array<std::vector<gymj::common::PlayerAction>, 4>& rhs,
    const std::string& message
){
    for(int seat = 0; seat < 4; ++seat){
        require(lhs[seat].size() == rhs[seat].size(), message);
        for(std::size_t i = 0; i < lhs[seat].size(); ++i){
            require(lhs[seat][i].type == rhs[seat][i].type, message);
            require(lhs[seat][i].action_tile == rhs[seat][i].action_tile, message);
        }
    }
}

void test_start_deals_initial_hands_and_emits_events(){
    std::mt19937 rng{123456};
    Round round{config_with_dealer(2), players(), &rng};

    const auto transition = round.start();
    const auto& state = round.state();

    require(transition.accepted, "start should be accepted for a new round");
    require(transition.error.empty(), "accepted start should not contain an error");
    require(transition.seq_before == 0, "start should begin at sequence zero");
    require(transition.seq_after > 0, "start should assign a positive sequence number");
    require(transition.stage_before == RoundStage::NotActive,
            "start transition should report NotActive as the previous stage");
    require(transition.stage_after == RoundStage::WaitingDraw,
            "start transition should wait for the dealer to draw");
    require(transition.actor_before == -1, "start should have no previous actor");
    require(transition.actor_after == 2, "start should make the dealer the actor");
    require(!transition.round_ended, "a started round should not be ended");
    require(!transition.round_result.has_value(), "start should not produce a round result");
    require(!transition.point_result.has_value(), "start should not calculate points");

    require(state.stage == RoundStage::WaitingDraw, "round state should wait for a draw");
    require(state.acting_player == 2, "round state should identify the dealer as actor");
    require(!state.pending_action.has_value(), "start should not leave a pending action");
    require(state.discard_detail == DiscardDetail::None,
            "start should not set a discard detail");

    require(transition.events.size() == 5,
            "start should emit one round event and four initial-hand events");
    require(transition.events[0].type == RoundEventType::RoundStarted,
            "the first start event should be RoundStarted");
    require(transition.events[0].seq == transition.seq_after,
            "RoundStarted should use the new sequence number");
    require(transition.events[0].player_seat == 2,
            "RoundStarted should identify the dealer");

    std::array<int, gymj::common::tileKindCount> tile_counts{};
    for(int seat = 0; seat < 4; ++seat){
        const auto& player_state = state.states[seat];
        require(player_state.hand.size() == 13,
                "start should deal exactly 13 tiles to each player");
        require(!player_state.draw_buffer.has_value(),
                "initial hands should not have a draw buffer");
        require(!player_state.discard_buffer.has_value(),
                "initial hands should not have a discard buffer");
        require(player_state.river.empty(), "initial rivers should be empty");
        require(player_state.melds.empty(), "initial melds should be empty");
        require(transition.available_actions[seat].empty(),
                "players should have no actions before the dealer draws");

        const auto& event = transition.events[seat + 1];
        require(event.type == RoundEventType::InitialHands,
                "start should emit an InitialHands event for every seat");
        require(event.seq == transition.seq_after,
                "InitialHands should use the start sequence number");
        require(event.player_seat == seat,
                "InitialHands should identify its owning seat");
        require(event.tiles == player_state.hand,
                "InitialHands event tiles should match round state");

        for(const Tile tile : player_state.hand){
            const int index = gymj::common::tile_index(tile);
            require(index >= 0, "start should only deal valid tiles");
            ++tile_counts[static_cast<std::size_t>(index)];
        }
    }

    int total_tiles = 0;
    for(const int count : tile_counts){
        require(count <= 4, "start should not deal more than four copies of a tile");
        total_tiles += count;
    }
    require(total_tiles == 52, "start should deal 52 tiles in total");
}

void test_start_rejects_repeated_call_without_mutation(){
    std::mt19937 rng{7};
    Round round{config_with_dealer(1), players(), &rng};
    const auto first = round.start();
    const auto state_before = round.state();

    const auto second = round.start();

    require(first.accepted, "first start should be accepted");
    require(!second.accepted, "second start should be rejected");
    require(second.error == "round already started",
            "repeated start should explain why it was rejected");
    require(second.seq_before == first.seq_after && second.seq_after == first.seq_after,
            "repeated start should not advance the sequence number");
    require(second.events.empty(), "repeated start should not emit events");
    require(round.state().stage == state_before.stage,
            "repeated start should preserve the round stage");
    require(round.state().acting_player == state_before.acting_player,
            "repeated start should preserve the acting player");
    for(int seat = 0; seat < 4; ++seat){
        require(round.state().states[seat].hand == state_before.states[seat].hand,
                "repeated start should not deal additional tiles");
    }
}

void test_start_rejects_invalid_four_player_configuration(){
    {
        std::mt19937 rng{11};
        auto config = config_with_dealer(4);
        Round round{config, players(), &rng};
        const auto transition = round.start();

        require(!transition.accepted, "start should reject an out-of-range dealer");
        require(transition.error == "invalid dealer seat",
                "invalid dealer rejection should be explicit");
        require(round.state().stage == RoundStage::NotActive,
                "invalid dealer should leave the round inactive");
    }

    {
        std::mt19937 rng{11};
        auto config = config_with_dealer(0);
        config.player_count = 3;
        config.rule.game.playerCount = 3;
        Round round{config, players(), &rng};
        const auto transition = round.start();

        require(!transition.accepted, "start should reject unsupported player counts");
        require(transition.error == "round requires exactly four players",
                "unsupported player count rejection should be explicit");
        require(round.state().stage == RoundStage::NotActive,
                "unsupported player count should leave the round inactive");
    }
}

void test_draw_before_start_is_rejected_without_mutation(){
    std::mt19937 rng{19};
    Round round{config_with_dealer(0), players(), &rng};

    const auto transition = round.draw_for_current_player();

    require(!transition.accepted, "draw before start should be rejected");
    require(transition.error == "can not draw", "invalid draw should return an error");
    require(transition.seq_before == 0 && transition.seq_after == 0,
            "rejected draw should not advance the sequence number");
    require(transition.stage_before == RoundStage::NotActive
                && transition.stage_after == RoundStage::NotActive,
            "rejected draw should preserve the stage");
    require(transition.actor_before == -1 && transition.actor_after == -1,
            "rejected draw should preserve the actor");
    require(transition.events.empty(), "rejected draw should not emit events");
    require(round.state().stage == RoundStage::NotActive,
            "draw before start should not mutate round state");
}

void test_draw_adds_one_tile_and_moves_to_waiting_discard(){
    std::mt19937 rng{2026};
    Round round{config_with_dealer(3), players(), &rng};
    const auto started = round.start();
    const auto dealer_hand_before = round.state().states[3].hand;

    const auto transition = round.draw_for_current_player();
    const auto& state = round.state();

    require(transition.accepted, "dealer draw should be accepted after start");
    require(transition.seq_before == started.seq_after,
            "draw should begin at the start sequence number");
    require(transition.seq_after == transition.seq_before + 1,
            "draw should advance the sequence number once");
    require(transition.stage_before == RoundStage::WaitingDraw,
            "draw should begin in WaitingDraw");
    require(transition.stage_after == RoundStage::WaitingDiscard,
            "draw should move to WaitingDiscard");
    require(transition.actor_before == 3 && transition.actor_after == 3,
            "dealer should remain the actor after drawing");
    require(!transition.round_ended, "normal draw should not end the round");
    require(transition.events.size() == 1, "normal draw should emit exactly one event");

    const auto& event = transition.events.front();
    require(event.type == RoundEventType::PlayerDraw,
            "draw should emit a PlayerDraw event");
    require(event.seq == transition.seq_after,
            "PlayerDraw should use the new sequence number");
    require(event.player_seat == 3, "PlayerDraw should identify the dealer");
    require(event.tile.has_value(), "PlayerDraw should contain the drawn tile");

    require(state.stage == RoundStage::WaitingDiscard,
            "round state should wait for a discard after drawing");
    require(state.acting_player == 3, "dealer should be the acting player after drawing");
    require(state.discard_detail == DiscardDetail::SimpleDraw,
            "normal draw should set SimpleDraw detail");
    require(!state.pending_action.has_value(), "draw should clear any pending action");
    require(state.states[3].hand.size() == 14,
            "drawing player should have 14 tiles");
    require(state.states[3].draw_buffer.has_value(),
            "drawing player should have a draw buffer");
    require(*state.states[3].draw_buffer == *event.tile,
            "draw buffer should match the draw event");
    require(state.states[3].hand.back() == *event.tile,
            "drawn tile should be appended to the hand");
    require(std::equal(dealer_hand_before.begin(), dealer_hand_before.end(),
                       state.states[3].hand.begin()),
            "draw should preserve the existing hand prefix");
    require(discard_action_count(transition, 3) == 14,
            "drawing player should be able to discard every hand tile");
    for(int seat = 0; seat < 3; ++seat){
        require(state.states[seat].hand.size() == 13,
                "draw should not change another player's hand");
        require(transition.available_actions[seat].empty(),
                "non-acting players should not receive actions after a draw");
    }
}

void test_second_draw_is_rejected_and_preserves_actions(){
    std::mt19937 rng{42};
    Round round{config_with_dealer(0), players(), &rng};
    round.start();
    const auto first_draw = round.draw_for_current_player();
    const auto state_before = round.state();

    const auto second_draw = round.draw_for_current_player();

    require(first_draw.accepted, "first draw should be accepted");
    require(!second_draw.accepted, "second draw before discard should be rejected");
    require(second_draw.error == "can not draw", "second draw should return an error");
    require(second_draw.seq_before == first_draw.seq_after
                && second_draw.seq_after == first_draw.seq_after,
            "rejected second draw should not advance the sequence number");
    require(second_draw.stage_before == RoundStage::WaitingDiscard
                && second_draw.stage_after == RoundStage::WaitingDiscard,
            "rejected second draw should preserve WaitingDiscard");
    require(second_draw.events.empty(), "rejected second draw should not emit events");
    require_same_actions(second_draw.available_actions, first_draw.available_actions,
                         "rejected draw should return the current legal actions");
    require(round.state().states[0].hand == state_before.states[0].hand,
            "rejected second draw should not add a tile");
    require(round.state().states[0].draw_buffer == state_before.states[0].draw_buffer,
            "rejected second draw should preserve the draw buffer");
}

}

int main(){
    try{
        test_start_deals_initial_hands_and_emits_events();
        test_start_rejects_repeated_call_without_mutation();
        test_start_rejects_invalid_four_player_configuration();
        test_draw_before_start_is_rejected_without_mutation();
        test_draw_adds_one_tile_and_moves_to_waiting_discard();
        test_second_draw_is_rejected_and_preserves_actions();
    }catch(const std::exception& ex){
        std::cerr << "round_tests failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "round_tests passed\n";
    return EXIT_SUCCESS;
}
