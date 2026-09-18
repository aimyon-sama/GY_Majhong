#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gymj/core/point/point_engine.hpp>

namespace{

using gymj::common::DashChicken;
using gymj::common::Meld;
using gymj::common::MeldType;
using gymj::common::PlayerTileState;
using gymj::common::PointResult;
using gymj::common::PointRuleConfig;
using gymj::common::RoundResult;
using gymj::common::Tile;
using gymj::common::TileType;
using gymj::common::WinDetail;
using gymj::common::WinType;
using gymj::rule::PointEngine;

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

PointEngine make_engine(){
    return PointEngine{PointRuleConfig{}};
}

PlayerTileState mixed_state(){
    PlayerTileState state{};
    state.hand = {
        m(2), m(3), m(4),
        s(2), s(3), s(4),
        p(2), p(3), p(4),
        m(5), m(6), m(7),
        s(9), s(9),
    };
    return state;
}

RoundResult base_winning_round(WinType win_type, int winner, int discarder, WinDetail detail = WinDetail::Simple){
    RoundResult round{};
    round.has_winner = true;
    round.winner_seat = winner;
    round.discarder_seat = discarder;
    round.win_tile = m(1);
    round.win_type = win_type;
    round.detail = detail;
    for(auto& state : round.states){
        state = mixed_state();
    }
    return round;
}

std::string delta_string(const PointResult& result){
    std::string text = "[";
    for(int i = 0; i < 4; ++i){
        if(i != 0){
            text += ", ";
        }
        text += std::to_string(result.delta_result[i]);
    }
    text += "]";
    return text;
}

std::string tile_text(Tile tile){
    return gymj::common::tile_to_string(tile);
}

std::string win_type_text(WinType type){
    switch(type){
        case WinType::NoWinner:
            return "NoWinner";
        case WinType::Ron:
            return "Ron";
        case WinType::Tsumo:
            return "Tsumo";
    }
    return "Unknown";
}

std::string win_detail_text(WinDetail detail){
    switch(detail){
        case WinDetail::NoWinner:
            return "NoWinner";
        case WinDetail::Simple:
            return "Simple";
        case WinDetail::TsumoFromKan:
            return "TsumoFromKan";
        case WinDetail::RonKanDiscard:
            return "RonKanDiscard";
        case WinDetail::RonAddKan:
            return "RonAddKan";
    }
    return "Unknown";
}

std::string meld_type_text(MeldType type){
    switch(type){
        case MeldType::Pon:
            return "Pon";
        case MeldType::OpenKan:
            return "OpenKan";
        case MeldType::SelfKan:
            return "SelfKan";
        case MeldType::AddKan:
            return "AddKan";
    }
    return "Unknown";
}

std::string tiles_text(const std::vector<Tile>& tiles){
    std::string text = "[";
    for(std::size_t i = 0; i < tiles.size(); ++i){
        if(i != 0){
            text += " ";
        }
        text += tile_text(tiles[i]);
    }
    text += "]";
    return text;
}

std::string melds_text(const std::vector<Meld>& melds){
    std::string text = "[";
    for(std::size_t i = 0; i < melds.size(); ++i){
        if(i != 0){
            text += ", ";
        }
        text += meld_type_text(melds[i].type);
        text += "(";
        text += tile_text(melds[i].tile);
        text += ", from ";
        text += std::to_string(melds[i].from_seat);
        text += ")";
    }
    text += "]";
    return text;
}

std::string dash_text(const std::optional<DashChicken>& dash){
    if(!dash.has_value()){
        return "none";
    }
    return tile_text(dash->tile)
        + " discarded_by=" + std::to_string(dash->discarded_by)
        + " claimed_by=" + std::to_string(dash->claimed_by)
        + " claim_type=" + meld_type_text(dash->claim_type);
}

std::string tenpai_text(const std::array<bool, 4>& tenpai){
    std::string text = "[";
    for(int i = 0; i < 4; ++i){
        if(i != 0){
            text += ", ";
        }
        text += tenpai[i] ? "T" : "N";
    }
    text += "]";
    return text;
}

void print_round(const std::string& name, const RoundResult& round, Tile round_chicken, const std::array<bool, 4>& tenpai){
    std::cout << "\ncase " << name << " round_result\n";
    std::cout << "  has_winner=" << round.has_winner
              << " win_type=" << win_type_text(round.win_type)
              << " detail=" << win_detail_text(round.detail)
              << " winner=" << round.winner_seat
              << " discarder=" << round.discarder_seat
              << " win_tile=" << tile_text(round.win_tile)
              << " round_chicken=" << tile_text(round_chicken)
              << " tenpai=" << tenpai_text(tenpai)
              << '\n';
    std::cout << "  one_sou=" << dash_text(round.one_sou) << '\n';
    std::cout << "  eight_pin=" << dash_text(round.eight_pin) << '\n';
    for(int i = 0; i < 4; ++i){
        std::cout << "  seat " << i
                  << " hand=" << tiles_text(round.states[i].hand)
                  << " river=" << tiles_text(round.states[i].river)
                  << " melds=" << melds_text(round.states[i].melds)
                  << '\n';
    }
}

void print_result(const std::string& name, const PointResult& result){
    std::cout << "case " << name << " point_result\n";
    std::cout << "  delta " << delta_string(result) << '\n';
    std::cout << "  detail\n";
    for(int i = 0; i < 4; ++i){
        std::cout << "    seat " << i
                  << " total=" << result.detail[i].total_point
                  << " chicken=" << result.detail[i].point_from_chicken
                  << " kan=" << result.detail[i].point_from_kan
                  << " agari=" << result.detail[i].point_from_agari
                  << " tenpai=" << result.detail[i].point_from_tenpai
                  << '\n';
    }
    std::cout << "  matrix point_to_others[payer][receiver]\n";
    for(int i = 0; i < 4; ++i){
        std::cout << "    payer " << i << ":";
        for(int j = 0; j < 4; ++j){
            std::cout << ' ' << result.point_to_others[i][j];
        }
        std::cout << '\n';
    }
}

void expect_delta(const PointResult& result, std::array<int, 4> expected, const std::string& name){
    for(int i = 0; i < 4; ++i){
        require(result.delta_result[i] == expected[i],
                name + " expected delta[" + std::to_string(i) + "]="
                + std::to_string(expected[i]) + ", got " + std::to_string(result.delta_result[i]));
    }
}

void test_no_winner_tenpai_payment(){
    auto engine = make_engine();
    RoundResult round{};
    round.has_winner = false;
    round.detail = WinDetail::NoWinner;
    for(auto& state : round.states){
        state = mixed_state();
    }

    const std::array<bool, 4> tenpai{true, true, false, false};
    const Tile round_chicken = p(9);
    print_round("no_winner_tenpai_payment", round, round_chicken, tenpai);
    const auto result = engine.calculate(round, round_chicken, tenpai);

    expect_delta(result, {6, 6, -6, -6}, "no_winner_tenpai_payment");
    require(result.point_to_others[2][0] == 3, "seat 2 should pay seat 0 tenpai value");
    require(result.point_to_others[2][1] == 3, "seat 2 should pay seat 1 tenpai value");
    require(result.detail[0].point_from_tenpai == 6, "seat 0 tenpai detail should match delta");
    require(result.detail[2].point_from_tenpai == -6, "seat 2 tenpai detail should match delta");
    print_result("no_winner_tenpai_payment", result);
}

void test_simple_ron(){
    auto engine = make_engine();
    auto round = base_winning_round(WinType::Ron, 0, 1);

    const std::array<bool, 4> tenpai{true, true, true, true};
    const Tile round_chicken = p(9);
    print_round("simple_ron", round, round_chicken, tenpai);
    const auto result = engine.calculate(round, round_chicken, tenpai);

    expect_delta(result, {11, -11, 0, 0}, "simple_ron");
    require(result.point_to_others[1][0] == 11, "discarder should pay winner including no-chicken-no-kan bonus");
    require(result.detail[0].point_from_agari == 11, "winner agari detail should include no-chicken-no-kan bonus");
    print_result("simple_ron", result);
}

void test_simple_tsumo(){
    auto engine = make_engine();
    auto round = base_winning_round(WinType::Tsumo, 0, -1);

    const std::array<bool, 4> tenpai{true, true, true, true};
    const Tile round_chicken = p(9);
    print_round("simple_tsumo", round, round_chicken, tenpai);
    const auto result = engine.calculate(round, round_chicken, tenpai);

    expect_delta(result, {33, -11, -11, -11}, "simple_tsumo");
    require(result.detail[0].point_from_agari == 33, "winner agari detail should collect three payments including no-chicken-no-kan bonus");
    print_result("simple_tsumo", result);
}

void test_kan_detail_tsumo_bonus(){
    auto engine = make_engine();
    auto round = base_winning_round(WinType::Tsumo, 0, -1, WinDetail::TsumoFromKan);

    const std::array<bool, 4> tenpai{true, true, true, true};
    const Tile round_chicken = p(9);
    print_round("kan_detail_tsumo_bonus", round, round_chicken, tenpai);
    const auto result = engine.calculate(round, round_chicken, tenpai);

    expect_delta(result, {51, -17, -17, -17}, "kan_detail_tsumo_bonus");
    require(result.detail[0].point_from_agari == 51, "kan detail tsumo should use boosted winner value");
    print_result("kan_detail_tsumo_bonus", result);
}

void test_claimed_dash_chicken(){
    auto engine = make_engine();
    auto round = base_winning_round(WinType::Ron, 2, 3);
    round.states[0] = mixed_state();
    round.states[0].melds.push_back(Meld{MeldType::Pon, s(1), 1});
    round.one_sou = DashChicken{s(1), 1, 0, MeldType::Pon};

    const std::array<bool, 4> tenpai{true, true, true, true};
    const Tile round_chicken = p(9);
    print_round("claimed_dash_chicken", round, round_chicken, tenpai);
    const auto result = engine.calculate(round, round_chicken, tenpai);

    print_result("claimed_dash_chicken", result);
    expect_delta(result, {11, -5, 8, -14}, "claimed_dash_chicken");
    require(result.point_to_others[1][0] == 5, "discarder of first 1s should pay pon claimer extra dash chicken");
    require(result.detail[0].point_from_chicken == 11, "claimer chicken detail should include meld chicken and dash bonus");
}

void test_kan_payments(){
    auto engine = make_engine();
    auto round = base_winning_round(WinType::Ron, 2, 3);
    round.states[0].melds.push_back(Meld{MeldType::OpenKan, m(9), 1});
    round.states[0].melds.push_back(Meld{MeldType::AddKan, p(7), 2});
    round.states[0].melds.push_back(Meld{MeldType::SelfKan, s(7), 0});

    const std::array<bool, 4> tenpai{true, true, true, true};
    const Tile round_chicken = p(9);
    print_round("kan_payments", round, round_chicken, tenpai);
    const auto result = engine.calculate(round, round_chicken, tenpai);

    expect_delta(result, {35, -15, 1, -21}, "kan_payments");
    require(result.point_to_others[1][0] == 15, "source should pay for open, added and concealed kans");
    require(result.point_to_others[2][0] == 10 && result.point_to_others[3][0] == 10,
            "non-source seats should pay only for added and concealed kans");
    require(result.detail[0].point_from_kan == 35, "open kan receives one payment, other kans receive three");
    print_result("kan_payments", result);
}

void test_multi_ron_does_not_repeat_shared_payments(){
    auto engine = make_engine();
    auto round = base_winning_round(WinType::Ron, 1, 3);
    round.states[0].river.push_back(s(1));
    round.states[0].melds.push_back(Meld{MeldType::SelfKan, m(9), 0});
    const std::array<bool, 4> tenpai{true, true, true, true};
    const auto single = engine.calculate(round, p(9), tenpai);
    round.winner_seats = {1, 2};
    const auto multi = engine.calculate(round, p(9), tenpai);
    for(int payer = 0; payer < 4; ++payer){
        require(multi.detail[payer].point_from_chicken == single.detail[payer].point_from_chicken,
                "multi ron must not repeat chicken payments");
        require(multi.detail[payer].point_from_kan == single.detail[payer].point_from_kan,
                "multi ron must not repeat kan payments");
        for(int receiver = 0; receiver < 4; ++receiver){
            const int extra = payer == 3 && receiver == 2 ? 11 : 0;
            require(multi.point_to_others[payer][receiver] == single.point_to_others[payer][receiver] + extra,
                    "multi ron should add only the second winner's agari payment");
        }
    }
}

void test_tenpai_nonwinner_collects_chicken_from_every_opponent(){
    PointRuleConfig config{};
    config.hand_chicken_point = 4;
    PointEngine engine{config};
    for(const auto detail : {WinDetail::Simple, WinDetail::RonKanDiscard}){
        for(const Tile tile : {s(1), p(8), s(6)}){
            auto round = base_winning_round(WinType::Ron, 0, 3, detail);
            round.states[1].river.push_back(tile);
            const auto result = engine.calculate(round, s(6), {true, true, false, false});
            for(const int payer : {0, 2, 3}){
                require(result.point_to_others[payer][1] == config.hand_chicken_point,
                        "winner and both other opponents must pay the tenpai player's chicken");
            }
            require(result.detail[1].point_from_chicken == 3 * config.hand_chicken_point,
                    "tenpai nonwinner should receive three chicken payments");
        }
    }
    auto round = base_winning_round(WinType::Ron, 0, 3);
    round.states[1].river.push_back(s(1));
    const auto overlap = engine.calculate(round, s(1), {true, true, false, false});
    require(overlap.point_to_others[0][1] == config.hand_chicken_point,
            "a fixed chicken that is also the round chicken must not be counted twice");
}

void test_multi_ron_scores_each_winners_shape(){
    auto engine = make_engine();
    auto round = base_winning_round(WinType::Ron, 1, 0);
    round.win_tile = m(5);
    round.states[1].hand = {m(1), m(1), m(2), m(2), m(3), m(3), m(6), m(6), m(7), m(7), m(8), m(8), m(5)};
    round.states[2].hand = {m(1), m(2), m(3), s(2), s(3), s(4), p(2), p(3), p(4), m(6), m(7), m(8), m(5)};
    round.states[3].hand = {p(1), p(2), p(3), s(2), s(3), s(4), m(2), m(3), m(4), p(6), p(7), p(8), m(5)};
    for(const auto winners : {std::vector<int>{1, 2}, std::vector<int>{1, 2, 3}}){
        round.winner_seats = winners;
        const auto result = engine.calculate(round, gymj::common::null_tile, {true, true, true, true});
        const bool triple = winners.size() == 3;
        require(result.detail[1].point_from_agari == 42, "pure seven pairs winner should receive its own shape value");
        require(result.detail[2].point_from_agari == 11, "mixed winner should receive its own no-chicken bonus");
        require(result.detail[3].point_from_agari == (triple ? 3 : 0),
                "third player should receive agari only when included among winners");
        expect_delta(result, triple ? std::array<int, 4>{-57, 41, 10, 6} : std::array<int, 4>{-54, 41, 10, 3},
                     "multi_ron_individual_shapes");
    }
}

void test_multi_ron_counts_winning_chicken_for_each_winner(){
    auto engine = make_engine();
    for(const Tile tile : {s(1), p(8), s(6)}){
        auto round = base_winning_round(WinType::Ron, 1, 0);
        round.winner_seats = {1, 2};
        round.win_tile = tile;
        if(tile == p(8)){
            round.states[1].hand = {m(1), m(2), m(3), m(4), m(5), m(6), s(2), s(3), s(4), p(6), p(7), s(9), s(9)};
        } else {
            round.states[1].hand = {m(1), m(2), m(3), m(4), m(5), m(6), p(2), p(3), p(4), p(5), p(5)};
            round.states[1].hand.push_back(tile == s(1) ? s(2) : s(4));
            round.states[1].hand.push_back(tile == s(1) ? s(3) : s(5));
        }
        round.states[2].hand = round.states[1].hand;
        const auto result = engine.calculate(round, s(6), {true, true, true, true});
        for(const int winner : round.winner_seats){
            require(result.detail[winner].point_from_chicken == 3,
                    "each winner must count the separately stored winning chicken exactly once per opponent");
            require(result.detail[winner].point_from_agari == 3,
                    "a winning chicken must exclude the no-chicken bonus for every winner");
        }
        expect_delta(result, {-8, 5, 5, -2}, "multi_ron_winning_chicken");
    }
}

void test_tenpai_nonwinner_collects_kan_payments_by_type(){
    PointRuleConfig config{};
    config.kan_point = 7;
    PointEngine engine{config};
    for(const auto type : {MeldType::OpenKan, MeldType::AddKan, MeldType::SelfKan}){
        auto round = base_winning_round(WinType::Ron, 0, 3, WinDetail::RonKanDiscard);
        round.states[1].melds.push_back(Meld{type, m(9), 2});
        const auto result = engine.calculate(round, p(9), {true, true, false, false});
        for(const int payer : {0, 2, 3}){
            const int expected = type != MeldType::OpenKan || payer == 2 ? config.kan_point : 0;
            require(result.point_to_others[payer][1] == expected,
                    "open kan is paid only by its source; added and concealed kans are paid by all opponents");
        }
        require(result.detail[1].point_from_kan == (type == MeldType::OpenKan ? 1 : 3) * config.kan_point,
                "kan income should reflect the number of liable opponents");
    }
}

void test_draw_ignores_chicken_and_kan_payments(){
    PointRuleConfig config{};
    config.hand_chicken_point = 4;
    config.kan_point = 7;
    PointEngine engine{config};
    RoundResult round{};
    for(auto& player : round.states){ player = mixed_state(); }
    const std::array<bool, 4> tenpai{true, true, false, false};
    const auto before = engine.calculate(round, gymj::common::null_tile, tenpai);
    round.states[1].river = {s(1), p(8), s(6)};
    round.states[1].melds = {
        Meld{MeldType::OpenKan, m(9), 2},
        Meld{MeldType::AddKan, p(7), 3},
        Meld{MeldType::SelfKan, s(7), 1},
    };
    round.one_sou = DashChicken{s(1), 1, -1, MeldType::Pon};
    round.eight_pin = DashChicken{p(8), 2, 1, MeldType::Pon};
    // These additions leave this fixture's mixed-suit shape value unchanged.
    const auto after = engine.calculate(round, s(6), tenpai);
    require(after.point_to_others == before.point_to_others,
            "fixed chickens, round chicken, dash chickens and kans must not add draw payments");
    expect_delta(after, {6, 6, -6, -6}, "draw_ignores_chicken_and_kan");
    for(int seat = 0; seat < 4; ++seat){
        require(after.detail[seat].point_from_tenpai == before.detail[seat].point_from_tenpai,
                "draw should preserve the hand-shape settlement");
        require(after.detail[seat].point_from_chicken == 0 && after.detail[seat].point_from_kan == 0,
                "draw should report zero chicken and kan payments");
        require(after.detail[seat].point_from_agari == 0
                && after.detail[seat].total_point == after.detail[seat].point_from_tenpai,
                "draw total should consist entirely of hand-shape payments");
    }
}

}

int main(){
    try{
        test_no_winner_tenpai_payment();
        test_draw_ignores_chicken_and_kan_payments();
        test_simple_ron();
        test_simple_tsumo();
        test_kan_detail_tsumo_bonus();
        test_claimed_dash_chicken();
        test_kan_payments();
        test_multi_ron_does_not_repeat_shared_payments();
        test_multi_ron_scores_each_winners_shape();
        test_multi_ron_counts_winning_chicken_for_each_winner();
        test_tenpai_nonwinner_collects_chicken_from_every_opponent();
        test_tenpai_nonwinner_collects_kan_payments_by_type();
    }catch(const std::exception& ex){
        std::cerr << "point_engine_tests failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "point_engine_tests passed\n";
    return EXIT_SUCCESS;
}
