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
using gymj::common::TileType;
using gymj::common::PlayerAction;
using gymj::common::MeldType;
using gymj::common::WinType;
using gymj::common::WinDetail;
using gymj::room::Wall;
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

Tile m(int rank){ return Tile{TileType::Man, static_cast<std::uint8_t>(rank)}; }
Tile s(int rank){ return Tile{TileType::Sou, static_cast<std::uint8_t>(rank)}; }
Tile p(int rank){ return Tile{TileType::Pin, static_cast<std::uint8_t>(rank)}; }

std::array<Tile, Wall::tile_count> scripted_wall(
    const std::array<std::vector<Tile>, 4>& hands,
    const std::vector<Tile>& draws = {}, const std::vector<Tile>& kan_draws = {}
){
    std::array<Tile, Wall::tile_count> tiles;
    tiles.fill(gymj::common::null_tile);
    std::array<int, gymj::common::tileKindCount> counts{};
    auto place = [&](int index, Tile tile){
        require(tiles[index] == gymj::common::null_tile, "fixture positions must not overlap");
        require(++counts[gymj::common::tile_index(tile)] <= 4, "fixture must use at most four copies");
        tiles[index] = tile;
    };
    for(int seat = 0; seat < 4; ++seat){
        require(hands[seat].size() <= 13, "fixture initial hands must fit");
        for(std::size_t i = 0; i < hands[seat].size(); ++i){
            place(static_cast<int>(i) * 4 + seat, hands[seat][i]);
        }
    }
    for(std::size_t i = 0; i < draws.size(); ++i){
        place(52 + static_cast<int>(i), draws[i]);
    }
    for(std::size_t i = 0; i < kan_draws.size(); ++i){
        place(106 - static_cast<int>(i / 2) * 2 + static_cast<int>(i % 2), kan_draws[i]);
    }
    int index = 0;
    for(auto& tile : tiles){
        if(tile == gymj::common::null_tile){
            while(counts[index] == 4){ ++index; }
            tile = gymj::common::tile_from_index(index);
            ++counts[index];
        }
    }
    return tiles;
}

gymj::common::RoundTransition act(Round& round, int seat, PlayerActionType type, Tile tile){
    auto transition = round.submit_action(seat, PlayerAction{type, tile});
    require(transition.accepted, "scripted action rejected: " + transition.error);
    return transition;
}

void pass_remaining(Round& round){
    for(int seat = 0; seat < 4 && round.state().stage == RoundStage::WaitingClaim; ++seat){
        if(!round.available_actions()[seat].empty()){
            require(round.handle_timeout(seat).accepted, "claim timeout should pass");
        }
    }
    require(round.state().stage != RoundStage::WaitingClaim, "all responses must resolve");
}

void check_conservation(const Round& round){
    int total = round.state().tiles_remaining;
    std::array<int, gymj::common::tileKindCount> counts{};
    auto count_tile = [&](Tile tile, int copies){
        const int index = gymj::common::tile_index(tile);
        require(index >= 0, "state must contain valid tiles");
        counts[index] += copies;
        require(counts[index] <= 4, "state must not duplicate physical tiles");
        total += copies;
    };
    for(const auto& player : round.state().states){
        for(const auto tile : player.hand){ count_tile(tile, 1); }
        for(const auto tile : player.river){ count_tile(tile, 1); }
        for(const auto& meld : player.melds){ count_tile(meld.tile, meld.type == MeldType::Pon ? 3 : 4); }
    }
    if(round.result() && round.result()->win_type == WinType::Ron){
        count_tile(round.result()->win_tile, 1);
    }
    if(round.result() && round.result()->chicken_indicator){
        count_tile(*round.result()->chicken_indicator, 1);
    }
    require(total == Wall::tile_count, "round must conserve all 108 physical tiles");
}

void test_seeded_start_and_wall_validation(){
    auto config = config_with_dealer(0);
    config.seed = (std::uint64_t{1} << 40) + 17;
    Round first{config}, second{config};
    require(first.start().accepted && second.start().accepted, "owned generators should work");
    require(first.initial_wall() == second.initial_wall(), "same seed should reproduce the wall");
    require(first.state().tiles_remaining == 56, "dealing should leave 56 tiles");

    Round invalid{config};
    auto tiles = scripted_wall({});
    tiles[0] = gymj::common::null_tile;
    require(!invalid.start(tiles).accepted, "invalid tile should reject the wall");
    tiles.fill(m(1));
    require(!invalid.start(tiles).accepted, "too many copies should reject the wall");
    require(invalid.state().seq == 0 && invalid.events().empty(), "invalid wall should not mutate round");
    require(invalid.start().accepted, "a valid retry should remain possible");
    config = config_with_dealer(-1);
    Round invalid_dealer{config};
    require(!invalid_dealer.start().accepted, "negative dealer must be rejected");
    config = config_with_dealer(0);
    config.rule.game.playerCount = 3;
    require(!Round{config}.start().accepted, "rule engine player count must agree");
}

void test_rejected_actions_and_timeouts(){
    Round round{config_with_dealer(0)};
    require(!round.submit_action(0, {PlayerActionType::Discard, m(1)}).accepted,
            "inactive round must reject actions");
    require(!round.settle().accepted, "inactive round must reject settlement");
    require(round.start().accepted && round.draw_for_current_player().accepted, "round should draw");
    const auto before = round.state();
    const auto event_count = round.events().size();
    for(const int seat : {-1, 4, 1}){
        require(!round.submit_action(seat, {PlayerActionType::Discard, m(1)}).accepted,
                "invalid or non-acting seats must not discard");
        require(!round.handle_timeout(seat).accepted, "non-acting timeout must be rejected");
    }
    require(!round.submit_action(0, {PlayerActionType::Discard, gymj::common::null_tile}).accepted,
            "invalid tile must be rejected");
    require(!round.submit_action(0, {PlayerActionType::None, m(1)}).accepted,
            "unsupported action must be rejected");
    require(round.state().seq == before.seq && round.events().size() == event_count,
            "rejections must not advance the sequence or event log");
    require(round.state().states[0].hand == before.states[0].hand,
            "rejections must not mutate hands");
    const auto transition = round.handle_timeout(0);
    require(transition.accepted, "acting player's timeout should discard");
    require(transition.events.front().tile == before.states[0].draw_buffer,
            "timeout after drawing should discard the drawn tile");
    pass_remaining(round);
    require(round.state().acting_player == 1 && round.state().stage == RoundStage::WaitingDraw,
            "unclaimed discard should advance to the next seat");
    require(!round.state().pending_action && !round.state().states[0].discard_buffer,
            "resolved discard should clear transient buffers");
    check_conservation(round);
}

void test_pon_and_open_kan_move_tiles_and_track_dash_chicken(){
    for(const bool kan : {false, true}){
        std::array<std::vector<Tile>, 4> hands{};
        hands[1] = {s(1), s(1), s(1)};
        Round round{config_with_dealer(0)};
        require(round.start(scripted_wall(hands, {s(1)}, {p(9)})).accepted, "fixture should start");
        round.draw_for_current_player();
        act(round, 0, PlayerActionType::Discard, s(1));
        const auto type = kan ? PlayerActionType::OpenKan : PlayerActionType::Pon;
        act(round, 1, type, s(1));
        pass_remaining(round);
        const auto& state = round.state();
        require(state.acting_player == 1, "claimant should become the actor");
        require(state.states[0].river.empty(), "claimed discard should leave the river");
        require(state.states[1].hand.size() == (kan ? 10u : 11u), "claim should consume matching tiles");
        require(state.states[1].melds.front().from_seat == 0, "meld should retain its source");
        require(state.one_sou && state.one_sou->claimed_by == 1,
                "first chicken claim should retain its owner");
        require(state.one_sou->claim_type == (kan ? MeldType::OpenKan : MeldType::Pon),
                "dash chicken should retain the claim type");
        if(kan){
            require(state.stage == RoundStage::WaitingDraw, "open kan should await replacement draw");
            const auto draw = round.draw_for_current_player();
            require(draw.accepted && draw.events.front().tile == std::optional<Tile>{p(9)},
                    "replacement draw must come from the tail");
            require(round.state().discard_detail == DiscardDetail::AfterOpenKanDraw,
                    "open kan draw should retain its detail");
        } else {
            require(state.stage == RoundStage::WaitingDiscard, "pon should skip drawing");
            const auto actions = round.available_actions();
            for(const auto& action : actions[1]){
                require(action.type == PlayerActionType::Discard, "pon should only allow discards");
            }
            require(round.handle_timeout(1).accepted, "timeout after pon should discard from hand");
        }
        check_conservation(round);
    }
}

void test_later_chicken_discard_does_not_claim_the_first(){
    std::array<std::vector<Tile>, 4> hands{};
    hands[1] = {s(1), s(1)};
    Round round{config_with_dealer(0)};
    round.start(scripted_wall(hands, {s(1), m(8), m(8), m(8), s(1)}));
    for(int i = 0; i < 4; ++i){
        require(round.draw_for_current_player().accepted, "script should draw");
        require(round.handle_timeout(i).accepted, "script should discard");
        pass_remaining(round);
    }
    round.draw_for_current_player();
    act(round, 0, PlayerActionType::Discard, s(1));
    act(round, 1, PlayerActionType::Pon, s(1));
    pass_remaining(round);
    require(round.state().one_sou->claimed_by == -1,
            "claiming a later identical discard must not take the first dash chicken");
    check_conservation(round);
}

void test_self_kan_and_replacement_tsumo_settlement(){
    std::array<std::vector<Tile>, 4> hands{};
    hands[0] = {p(7), p(7), p(7), m(1), m(2), m(3), s(1), s(2), s(3), p(1), p(2), p(3), m(9)};
    Round round{config_with_dealer(0)};
    round.start(scripted_wall(hands, {p(7), p(8)}, {m(9)}));
    round.draw_for_current_player();
    act(round, 0, PlayerActionType::SelfKan, p(7));
    require(round.state().states[0].hand.size() == 10, "self kan should remove four tiles");
    require(!round.state().states[0].draw_buffer, "self kan should clear draw buffer");
    require(round.state().states[0].melds.front().type == MeldType::SelfKan, "self kan should create meld");
    const auto draw = round.draw_for_current_player();
    require(draw.accepted && draw.events.front().tile == std::optional<Tile>{m(9)}, "kan should draw tail tile");
    const auto win = act(round, 0, PlayerActionType::Tsumo, m(9));
    require(win.round_ended && win.round_result, "tsumo should end the round");
    require(win.round_result->detail == WinDetail::TsumoFromKan, "replacement tsumo should retain kan detail");
    require(win.round_result->states[0].hand.size() == 10 && round.state().states[0].hand.size() == 11,
            "settlement snapshot should separate winning tile from live hand");
    require(!win.point_result, "points should await settlement");
    require(win.round_result->chicken_indicator == std::optional<Tile>{p(8)},
            "kan tsumo should reveal from the remaining head, not the kan end");
    const auto settled = round.settle();
    require(settled.accepted && settled.point_result, "ended round should settle");
    require(settled.round_result->round_chicken == p(9), "settlement should retain the selected chicken");
    int sum = 0;
    for(const int delta : settled.point_result->delta_result){ sum += delta; }
    require(sum == 0, "settlement must be zero-sum");
    require(settled.point_result->detail[0].point_from_agari == 51, "kan tsumo should pay 17 from each opponent");
    const auto seq = round.state().seq;
    require(!round.settle().accepted && round.state().seq == seq, "settlement must be idempotently rejected");
    require(!round.draw_for_current_player().accepted, "ended round cannot draw");
    require(!round.handle_timeout(0).accepted, "ended round cannot time out");
    require(!round.start().accepted, "ended round cannot restart");
    check_conservation(round);
}

void test_add_kan_is_deferred_and_can_be_robbed(){
    for(const bool rob : {false, true}){
        std::array<std::vector<Tile>, 4> hands{};
        hands[1] = {p(5), p(5), m(8), m(1), m(2), m(3), s(4), s(5), s(6), p(1), p(2), p(3), m(7)};
        hands[2] = {m(1), m(2), m(3), s(1), s(2), s(3), p(7), p(8), p(9), m(9), m(9), p(3), p(4)};
        Round round{config_with_dealer(0)};
        round.start(scripted_wall(hands, {p(5), m(8), m(8), m(8), p(5)}, {m(7)}));
        round.draw_for_current_player();
        act(round, 0, PlayerActionType::Discard, p(5));
        act(round, 1, PlayerActionType::Pon, p(5));
        pass_remaining(round);
        act(round, 1, PlayerActionType::Discard, m(8));
        pass_remaining(round);
        for(const int seat : {2, 3, 0}){
            require(round.state().acting_player == seat, "script should preserve turn order");
            round.draw_for_current_player();
            round.handle_timeout(seat);
            pass_remaining(round);
        }
        round.draw_for_current_player();
        act(round, 1, PlayerActionType::AddKan, p(5));
        require(round.state().stage == RoundStage::WaitingClaim, "add kan should open a rob window");
        require(round.state().states[1].melds.front().type == MeldType::Pon,
                "pending add kan must leave pon unchanged");
        require(round.state().states[1].hand.size() == 11, "pending added tile must stay in hand");
        if(rob){
            act(round, 2, PlayerActionType::Ron, p(5));
            pass_remaining(round);
            require(round.result() && round.result()->detail == WinDetail::RonAddKan,
                    "robbed add kan should end in RonAddKan");
            require(round.result()->discarder_seat == 1, "robbed player should be the payer");
            require(round.state().states[1].melds.front().type == MeldType::Pon,
                    "robbed kan must remain a pon");
            require(round.state().states[1].hand.size() == 10, "robbed tile should leave declarer's hand");
        } else {
            pass_remaining(round);
            require(round.state().states[1].melds.front().type == MeldType::AddKan,
                    "unrobbed add kan should upgrade the pon");
            require(round.state().states[1].melds.front().from_seat == 0,
                    "add kan should preserve the original pon source");
            round.draw_for_current_player();
            require(round.state().discard_detail == DiscardDetail::AfterAddKanDraw,
                    "add kan should draw with the right detail");
            act(round, 1, PlayerActionType::Tsumo, m(7));
        }
        require(round.settle().accepted, "both add-kan outcomes should settle");
        check_conservation(round);
    }
}

std::vector<Tile> pure_wait(){
    return {m(1), m(2), m(3), m(1), m(2), m(3), m(6), m(7), m(8), m(6), m(7), m(8), m(5)};
}

void test_multi_ron_waits_for_responses_and_obeys_config(){
    for(const bool multi : {false, true}){
        auto config = config_with_dealer(0);
        config.rule.game.allowMultiRon = multi;
        Round round{config};
        std::array<std::vector<Tile>, 4> hands{};
        hands[1] = hands[2] = pure_wait();
        round.start(scripted_wall(hands, {m(5), s(5)}));
        round.draw_for_current_player();
        act(round, 0, PlayerActionType::Discard, m(5));
        act(round, 2, PlayerActionType::Ron, m(5));
        require(round.state().stage == RoundStage::WaitingClaim, "earlier response must not settle prematurely");
        require(round.available_actions()[2].empty(), "answered player must receive no further actions");
        const auto seq = round.state().seq;
        require(!round.submit_action(2, {PlayerActionType::Pass, m(5)}).accepted,
                "player cannot replace an already submitted claim");
        require(round.state().seq == seq, "duplicate response must not advance sequence");
        act(round, 1, PlayerActionType::Ron, m(5));
        pass_remaining(round);
        require(round.result()->winner_seats == (multi ? std::vector<int>{1, 2} : std::vector<int>{1}),
                "winners should follow seat order and multi-ron configuration");
        require(round.result()->win_type == WinType::Ron && round.result()->detail == WinDetail::Simple,
                "normal discard ron should have simple detail");
        require(round.state().states[0].river.empty(), "winning tile should be separated from river");
        const auto settled = round.settle();
        require(settled.accepted, "multi ron should settle");
        require(settled.point_result->detail[1].point_from_agari == 42,
                "pure seven pairs should receive two same-color values plus no-chicken bonus");
        require(settled.point_result->detail[2].point_from_agari == (multi ? 42 : 0),
                "only selected winners should receive agari payments");
        check_conservation(round);
    }
}

void test_ron_has_priority_over_pon(){
    Round round{config_with_dealer(0)};
    std::array<std::vector<Tile>, 4> hands{};
    hands[1] = {m(5), m(5)};
    hands[2] = pure_wait();
    round.start(scripted_wall(hands, {m(5)}));
    round.draw_for_current_player();
    act(round, 0, PlayerActionType::Discard, m(5));
    act(round, 1, PlayerActionType::Pon, m(5));
    require(round.state().states[1].melds.empty(), "pending pon must not mutate melds");
    act(round, 2, PlayerActionType::Ron, m(5));
    pass_remaining(round);
    require(round.result()->winner_seat == 2 && round.state().states[1].melds.empty(),
            "ron must override the earlier pon request");
    check_conservation(round);
}

void test_simple_tsumo_and_winning_chicken_bonus(){
    std::array<std::vector<Tile>, 4> hands{};
    hands[0] = {m(1), m(2), m(3), m(4), m(5), m(6), s(2), s(3), s(4), p(6), p(7), s(9), s(9)};
    Round round{config_with_dealer(0)};
    round.start(scripted_wall(hands, {p(8), p(8)}));
    round.draw_for_current_player();
    act(round, 0, PlayerActionType::Tsumo, p(8));
    require(round.result()->detail == WinDetail::Simple, "normal tsumo should have simple detail");
    const auto settled = round.settle();
    require(settled.point_result->detail[0].point_from_agari == 9,
            "winning chicken tile should disqualify no-chicken bonus");
    require(settled.point_result->detail[0].point_from_chicken == 3,
            "winning chicken tile must be counted exactly once against each opponent");
    check_conservation(round);
}

void test_ron_after_kan_draw_retains_detail(){
    std::array<std::vector<Tile>, 4> hands{};
    hands[0] = {s(9), s(9), s(9), m(1), m(2), m(3), s(4), s(5), s(6), p(1), p(2), p(3), m(9)};
    hands[1] = {m(4), m(5), m(6), s(1), s(2), s(3), p(7), p(8), p(9), m(9), m(9), p(3), p(4)};
    Round round{config_with_dealer(0)};
    round.start(scripted_wall(hands, {s(9)}, {p(5)}));
    round.draw_for_current_player();
    act(round, 0, PlayerActionType::SelfKan, s(9));
    round.draw_for_current_player();
    act(round, 0, PlayerActionType::Discard, p(5));
    act(round, 1, PlayerActionType::Ron, p(5));
    pass_remaining(round);
    require(round.result()->detail == WinDetail::RonKanDiscard,
            "discard immediately after a kan draw should retain RonKanDiscard");
    const auto settlement = round.settle();
    require(settlement.point_result->detail[1].point_from_agari == 17,
            "kan discard ron should use the kan win value");
    check_conservation(round);
}

void test_empty_wall_disallows_kan_but_keeps_final_discard(){
    std::array<std::vector<Tile>, 4> hands{};
    hands[3] = {p(9), p(9), p(9)};
    Round round{config_with_dealer(0)};
    round.start(scripted_wall(hands, {}, {p(8), p(9)}));
    while(round.state().tiles_remaining > 1){
        require(round.draw_for_current_player().accepted, "exhaustion fixture should draw");
        require(round.handle_timeout(round.state().acting_player).accepted, "exhaustion fixture should discard");
        pass_remaining(round);
    }
    require(round.state().acting_player == 3, "last tile should be drawn by seat 3");
    round.draw_for_current_player();
    require(round.state().tiles_remaining == 0 && round.state().stage == RoundStage::WaitingDiscard,
            "last draw should still permit a player action");
    const auto before = round.state();
    require(!round.submit_action(3, {PlayerActionType::SelfKan, p(9)}).accepted,
            "kan must be rejected without a replacement tile");
    require(round.state().seq == before.seq && round.state().states[3].hand == before.states[3].hand,
            "rejected final-tile kan must preserve state");
    round.handle_timeout(3);
    pass_remaining(round);
    const auto ended = round.draw_for_current_player();
    require(ended.accepted && ended.round_ended && !ended.round_result->has_winner,
            "exhaustion should occur after final discard responses finish");
    check_conservation(round);
}

void test_full_round_exhaustion_and_random_legal_play(){
    for(int seed = 0; seed < 24; ++seed){
        auto config = config_with_dealer(seed % 4);
        config.seed = static_cast<std::uint64_t>(seed);
        Round round{config};
        std::mt19937 decisions{static_cast<std::uint32_t>(seed)};
        round.start();
        int steps = 0;
        while(round.state().stage != RoundStage::Ended && steps++ < 500){
            const auto seq = round.state().seq;
            gymj::common::RoundTransition transition;
            if(round.state().stage == RoundStage::WaitingDraw){
                transition = round.draw_for_current_player();
            } else {
                const auto actions = round.available_actions();
                int seat = round.state().acting_player;
                if(round.state().stage == RoundStage::WaitingClaim){
                    seat = 0;
                    while(seat < 4 && actions[seat].empty()){ ++seat; }
                    require(seat < 4, "claim window must have someone left to answer");
                }
                if(seed < 12){
                    transition = round.handle_timeout(seat);
                } else {
                    const auto& offered = actions[seat];
                    require(!offered.empty(), "active player must have a legal action");
                    transition = round.submit_action(seat, offered[decisions() % offered.size()]);
                }
            }
            require(transition.accepted, "legal simulation action must succeed");
            require(transition.seq_before == seq && transition.seq_after == seq + 1,
                    "accepted transition must increment sequence once");
            for(const auto& event : transition.events){
                require(event.seq == transition.seq_after, "events must use the accepted transition sequence");
            }
            check_conservation(round);
        }
        require(round.state().stage == RoundStage::Ended, "simulation must terminate");
        if(seed < 12){
            require(!round.result()->has_winner && round.result()->winner_seat == -1,
                    "timeout-only simulation should end without a winner");
            require(round.state().tiles_remaining == 0, "exhaustion must consume the wall");
            require(round.result()->win_type == WinType::NoWinner, "exhaustion result should have no win type");
        }
        if(seed < 12){
            require(!round.result()->chicken_indicator
                    && round.result()->round_chicken == gymj::common::null_tile,
                    "empty-wall rounds must not reveal a chicken");
        }
        const auto settled = round.settle();
        require(settled.accepted && settled.point_result, "completed simulation must settle");
        int sum = 0;
        for(const int delta : settled.point_result->delta_result){ sum += delta; }
        require(sum == 0, "simulation settlement must remain zero-sum");
    }
}

void test_chicken_reveal_uses_head_and_wraps_each_suit(){
    for(const Tile indicator : {s(5), m(9), s(9), p(9)}){
        std::array<std::vector<Tile>, 4> hands{};
        hands[0] = {m(1), m(2), m(3), m(4), m(5), m(6), s(2), s(3), s(4), p(1), p(2), p(3), p(5)};
        Round round{config_with_dealer(0)};
        require(round.start(scripted_wall(hands, {p(5), indicator})).accepted, "reveal fixture should start");
        round.draw_for_current_player();
        const int remaining = round.state().tiles_remaining;
        const auto win = act(round, 0, PlayerActionType::Tsumo, p(5));
        const Tile expected{indicator.type, static_cast<std::uint8_t>(indicator.rank == 9 ? 1 : indicator.rank + 1)};
        require(win.round_result->chicken_indicator == std::optional<Tile>{indicator},
                "winning action should reveal the actual next head tile");
        require(win.round_result->round_chicken == expected, "chicken should advance within the same suit");
        require(round.state().tiles_remaining == remaining - 1, "revealing should consume exactly one tile");
        require(win.events.back().type == RoundEventType::ChickenRevealed
                && win.events.back().tile == std::optional<Tile>{indicator}
                && win.events.back().seq == win.seq_after,
                "winning transition should include a sequenced indicator event");
        const int remaining_after_reveal = round.state().tiles_remaining;
        const auto settlement = round.settle();
        require(settlement.accepted && settlement.round_result->round_chicken == expected,
                "settlement must use the already revealed chicken");
        require(round.state().tiles_remaining == remaining_after_reveal, "settlement must not draw again");
        require(!round.settle().accepted && round.state().tiles_remaining == remaining_after_reveal,
                "repeated settlement must not reveal again");
        check_conservation(round);
    }
}

void test_last_tile_tsumo_has_no_chicken_reveal(){
    std::array<std::vector<Tile>, 4> hands{};
    hands[3] = {m(1), m(2), m(3), m(4), m(5), m(6), s(2), s(3), s(4), p(1), p(2), p(3), p(5)};
    Round round{config_with_dealer(0)};
    require(round.start(scripted_wall(hands, {}, {s(5), p(5)})).accepted, "last tile fixture should start");
    while(round.state().tiles_remaining > 1){
        round.draw_for_current_player();
        round.handle_timeout(round.state().acting_player);
        pass_remaining(round);
    }
    require(round.state().acting_player == 3, "last tile must go to the scripted winner");
    round.draw_for_current_player();
    const auto win = act(round, 3, PlayerActionType::Tsumo, p(5));
    require(!win.round_result->chicken_indicator
            && win.round_result->round_chicken == gymj::common::null_tile,
            "winning with an empty wall must not reveal a chicken");
    for(const auto& event : win.events){
        require(event.type != RoundEventType::ChickenRevealed, "empty wall must not emit a reveal");
    }
    require(round.settle().accepted, "last tile win must settle without a round chicken");
    check_conservation(round);
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
        test_seeded_start_and_wall_validation();
        test_rejected_actions_and_timeouts();
        test_pon_and_open_kan_move_tiles_and_track_dash_chicken();
        test_later_chicken_discard_does_not_claim_the_first();
        test_self_kan_and_replacement_tsumo_settlement();
        test_add_kan_is_deferred_and_can_be_robbed();
        test_multi_ron_waits_for_responses_and_obeys_config();
        test_ron_has_priority_over_pon();
        test_simple_tsumo_and_winning_chicken_bonus();
        test_ron_after_kan_draw_retains_detail();
        test_empty_wall_disallows_kan_but_keeps_final_discard();
        test_full_round_exhaustion_and_random_legal_play();
        test_chicken_reveal_uses_head_and_wraps_each_suit();
        test_last_tile_tsumo_has_no_chicken_reveal();
    }catch(const std::exception& ex){
        std::cerr << "round_tests failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "round_tests passed\n";
    return EXIT_SUCCESS;
}
