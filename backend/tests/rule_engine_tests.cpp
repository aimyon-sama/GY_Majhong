#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gymj/core/rules/rule_engine.hpp>

namespace{

using gymj::common::DiscardDetail;
using gymj::common::GameRuleConfig;
using gymj::common::Meld;
using gymj::common::MeldType;
using gymj::common::PlayerAction;
using gymj::common::PlayerActionType;
using gymj::common::PlayerTileState;
using gymj::common::PointRuleConfig;
using gymj::common::PointResult;
using gymj::common::RoundResult;
using gymj::common::RoundStage;
using gymj::common::RoundState;
using gymj::common::Tile;
using gymj::common::TileType;
using gymj::common::WinDetail;
using gymj::rule::RuleEngine;

Tile m(int rank){
    return Tile{TileType::Man, static_cast<std::uint8_t>(rank)};
}

Tile s(int rank){
    return Tile{TileType::Sou, static_cast<std::uint8_t>(rank)};
}

Tile p(int rank){
    return Tile{TileType::Pin, static_cast<std::uint8_t>(rank)};
}

void require(bool condition, const std::string& message){
    if(!condition){
        throw std::runtime_error(message);
    }
}

RuleEngine make_engine(bool allow_multi_ron = true){
    PointRuleConfig point_config{};
    GameRuleConfig game_config{};
    game_config.allowMultiRon = allow_multi_ron;
    return RuleEngine{point_config, game_config};
}

bool has_action(const std::vector<PlayerAction>& actions, PlayerActionType type, Tile tile){
    for(const auto& action : actions){
        if(action.type == type && action.action_tile == tile){
            return true;
        }
    }
    return false;
}

int count_action_type(const std::vector<PlayerAction>& actions, PlayerActionType type){
    int count = 0;
    for(const auto& action : actions){
        if(action.type == type){
            ++count;
        }
    }
    return count;
}

std::vector<Tile> one_tile_wait_hand(){
    return {
        m(1), m(2), m(3),
        m(4), m(5), m(6),
        s(1), s(2), s(3),
        p(7), p(8), p(9),
        p(5),
    };
}

std::vector<Tile> non_winning_hand(){
    return {
        m(1), m(1), m(2),
        m(2), m(3), m(4),
        m(5), s(1), s(3),
        s(5), p(2), p(4),
        p(6),
    };
}

PlayerTileState tenpai_state(){
    PlayerTileState state{};
    state.hand = one_tile_wait_hand();
    return state;
}

PlayerTileState noten_state(){
    PlayerTileState state{};
    state.hand = non_winning_hand();
    return state;
}

void test_inactive_stages_offer_no_actions(){
    const auto engine = make_engine();
    for(const auto stage : {RoundStage::NotActive, RoundStage::WaitingDraw, RoundStage::Ended}){
        RoundState state{};
        state.stage = stage;
        const auto actions = engine.get_available_actions(state);
        for(const auto& seat_actions : actions){
            require(seat_actions.empty(), "inactive stages should not offer actions");
        }
    }
}

void test_waiting_discard_after_draw_uses_draw_buffer(){
    const auto engine = make_engine();
    RoundState state{};
    state.stage = RoundStage::WaitingDiscard;
    state.acting_player = 2;
    state.discard_detail = DiscardDetail::SimpleDraw;
    state.states[2].hand = one_tile_wait_hand();
    state.states[2].draw_buffer = p(5);
    state.states[2].hand.push_back(*state.states[2].draw_buffer);

    const auto actions = engine.get_available_actions(state);

    require(actions[0].empty(), "non-acting seat 0 should not get discard actions");
    require(actions[1].empty(), "non-acting seat 1 should not get discard actions");
    require(actions[3].empty(), "non-acting seat 3 should not get discard actions");
    require(has_action(actions[2], PlayerActionType::Discard, p(5)),
            "acting player should be allowed to discard the drawn tile");
    require(has_action(actions[2], PlayerActionType::Tsumo, p(5)),
            "acting player should be allowed to tsumo with hand plus draw_buffer");
}

void test_waiting_discard_after_draw_detects_kans_from_draw_buffer(){
    const auto engine = make_engine();

    RoundState self_kan_state{};
    self_kan_state.stage = RoundStage::WaitingDiscard;
    self_kan_state.acting_player = 0;
    self_kan_state.discard_detail = DiscardDetail::SimpleDraw;
    self_kan_state.states[0].hand = {
        p(7), p(7), p(7),
        m(1), m(2), m(3),
        m(4), m(5), m(6),
        s(1), s(2), s(3),
        p(9),
    };
    self_kan_state.states[0].draw_buffer = p(7);
    self_kan_state.states[0].hand.push_back(*self_kan_state.states[0].draw_buffer);

    const auto self_kan_actions = engine.get_available_actions(self_kan_state);
    require(has_action(self_kan_actions[0], PlayerActionType::SelfKan, p(7)),
            "acting player should be allowed to self kan with three hand tiles plus draw_buffer");

    RoundState add_kan_state{};
    add_kan_state.stage = RoundStage::WaitingDiscard;
    add_kan_state.acting_player = 1;
    add_kan_state.discard_detail = DiscardDetail::SimpleDraw;
    add_kan_state.states[1].hand = non_winning_hand();
    add_kan_state.states[1].melds.push_back(Meld{MeldType::Pon, m(9), 3});
    add_kan_state.states[1].draw_buffer = m(9);
    add_kan_state.states[1].hand.push_back(*add_kan_state.states[1].draw_buffer);

    const auto add_kan_actions = engine.get_available_actions(add_kan_state);
    require(has_action(add_kan_actions[1], PlayerActionType::AddKan, m(9)),
            "acting player should be allowed to add kan from a matching pon and draw_buffer");
}

void test_waiting_discard_after_pon_only_allows_discards(){
    const auto engine = make_engine();
    RoundState state{};
    state.stage = RoundStage::WaitingDiscard;
    state.acting_player = 1;
    state.discard_detail = DiscardDetail::AfterPon;
    state.states[1].hand = one_tile_wait_hand();

    const auto actions = engine.get_available_actions(state);

    require(count_action_type(actions[1], PlayerActionType::Discard) == static_cast<int>(state.states[1].hand.size()),
            "after pon the acting player should only discard from hand");
    require(count_action_type(actions[1], PlayerActionType::Tsumo) == 0,
            "after pon the acting player should not receive tsumo action");
    require(count_action_type(actions[1], PlayerActionType::SelfKan) == 0,
            "after pon the acting player should not receive self kan action");
}

void test_waiting_claim_offers_pon_open_kan_ron_and_pass(){
    const auto engine = make_engine();
    RoundState state{};
    state.stage = RoundStage::WaitingClaim;
    state.acting_player = 0;
    state.discard_detail = DiscardDetail::AfterSelfKanDraw;
    state.pending_action = PlayerAction{PlayerActionType::Discard, p(5)};

    state.states[1].hand = one_tile_wait_hand();
    state.states[2].hand = {p(5), p(5), m(1), m(2), m(3), s(1), s(2), s(3), p(1), p(2), p(3), m(7), m(8)};
    state.states[3].hand = {p(5), p(5), p(5), m(1), m(2), m(3), s(1), s(2), s(3), p(1), p(2), p(3), m(7)};

    const auto actions = engine.get_available_actions(state);

    require(actions[0].empty(), "discarding seat should not claim its own discard");
    require(has_action(actions[1], PlayerActionType::Ron, p(5)),
            "winning claimant should receive ron when the discard follows a kan draw");
    require(has_action(actions[1], PlayerActionType::Pass, p(5)),
            "claimant with any response should also receive pass");
    require(has_action(actions[2], PlayerActionType::Pon, p(5)),
            "claimant with two matching tiles should receive pon");
    require(!has_action(actions[2], PlayerActionType::OpenKan, p(5)),
            "claimant with only two matching tiles should not receive open kan");
    require(has_action(actions[3], PlayerActionType::Pon, p(5)),
            "claimant with three matching tiles should receive pon");
    require(has_action(actions[3], PlayerActionType::OpenKan, p(5)),
            "claimant with three matching tiles should receive open kan");
    require(has_action(actions[3], PlayerActionType::Pass, p(5)),
            "open kan claimant should also receive pass");
}

void test_add_kan_claim_only_allows_ron_and_pass(){
    const auto engine = make_engine();
    RoundState state{};
    state.stage = RoundStage::WaitingClaim;
    state.acting_player = 0;
    state.discard_detail = DiscardDetail::SimpleDraw;
    state.pending_action = PlayerAction{PlayerActionType::AddKan, p(5)};
    state.states[1].hand = one_tile_wait_hand();
    state.states[2].hand = {p(5), p(5), p(5), m(1), m(2), m(3), s(1), s(2), s(3), p(1), p(2), p(3), m(7)};
    state.states[3].hand = non_winning_hand();

    const auto actions = engine.get_available_actions(state);

    require(has_action(actions[1], PlayerActionType::Ron, p(5)),
            "add kan claim should allow ron against the added tile");
    require(has_action(actions[1], PlayerActionType::Pass, p(5)),
            "add kan ron claimant should also receive pass");
    require(actions[2].empty(),
            "add kan claim should not allow pon or open kan responses");
}

void test_resolve_claims_prioritizes_ron_and_multi_ron_config(){
    std::array<PlayerAction, 4> claims{};
    claims[0] = PlayerAction{PlayerActionType::Pass, p(5)};
    claims[1] = PlayerAction{PlayerActionType::Ron, p(5)};
    claims[2] = PlayerAction{PlayerActionType::Pon, p(5)};
    claims[3] = PlayerAction{PlayerActionType::Ron, p(5)};

    auto multi_result = make_engine(true).resolve_claims(claims, 0);
    require(multi_result.size() == 2, "multi ron should keep every ron claim");
    require(multi_result[0].first == 1, "multi ron should preserve turn order for first ron");
    require(multi_result[1].first == 3, "multi ron should preserve turn order for second ron");

    auto single_result = make_engine(false).resolve_claims(claims, 0);
    require(single_result.size() == 1, "single ron config should keep only first ron claim");
    require(single_result[0].first == 1, "single ron config should pick first ron in turn order");
}

void test_resolve_claims_picks_nearest_non_win_claim(){
    std::array<PlayerAction, 4> claims{};
    claims[1] = PlayerAction{PlayerActionType::Pon, p(5)};
    claims[3] = PlayerAction{PlayerActionType::OpenKan, p(5)};

    auto result_from_seat_0 = make_engine().resolve_claims(claims, 0);
    require(result_from_seat_0.size() == 1, "non-win claims should resolve to one action");
    require(result_from_seat_0[0].first == 1, "nearest non-win claimant after seat 0 should win");

    auto result_from_seat_2 = make_engine().resolve_claims(claims, 2);
    require(result_from_seat_2.size() == 1, "non-win claims should resolve to one action from seat 2");
    require(result_from_seat_2[0].first == 3, "nearest non-win claimant after seat 2 should win");
}

void test_calculate_points_derives_tenpai_from_round_result(){
    auto engine = make_engine();
    RoundResult round{};
    round.has_winner = false;
    round.detail = WinDetail::NoWinner;
    round.states[0] = tenpai_state();
    round.states[1] = tenpai_state();
    round.states[2] = noten_state();
    round.states[3] = noten_state();

    const PointResult result = engine.calculate_points(round, p(9));

    require(result.delta_result[0] == 6, "tenpai seat 0 should receive noten payments");
    require(result.delta_result[1] == 6, "tenpai seat 1 should receive noten payments");
    require(result.delta_result[2] == -6, "noten seat 2 should pay tenpai seats");
    require(result.delta_result[3] == -6, "noten seat 3 should pay tenpai seats");
}

}

int main(){
    try{
        test_inactive_stages_offer_no_actions();
        test_waiting_discard_after_draw_uses_draw_buffer();
        test_waiting_discard_after_draw_detects_kans_from_draw_buffer();
        test_waiting_discard_after_pon_only_allows_discards();
        test_waiting_claim_offers_pon_open_kan_ron_and_pass();
        test_add_kan_claim_only_allows_ron_and_pass();
        test_resolve_claims_prioritizes_ron_and_multi_ron_config();
        test_resolve_claims_picks_nearest_non_win_claim();
        test_calculate_points_derives_tenpai_from_round_result();
    }catch(const std::exception& ex){
        std::cerr << "rule_engine_tests failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "rule_engine_tests passed\n";
    return EXIT_SUCCESS;
}
