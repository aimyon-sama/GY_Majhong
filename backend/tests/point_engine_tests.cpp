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
    expect_delta(result, {11, -5, 0, -6}, "claimed_dash_chicken");
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

    expect_delta(result, {35, -15, -7, -13}, "kan_payments");
    require(result.point_to_others[1][0] == 15, "open kan source plus two all-pay kans should charge seat 1");
    require(result.detail[0].point_from_kan == 35, "kan detail should include all kan payments received");
    print_result("kan_payments", result);
}

}

int main(){
    try{
        test_no_winner_tenpai_payment();
        test_simple_ron();
        test_simple_tsumo();
        test_kan_detail_tsumo_bonus();
        test_claimed_dash_chicken();
        test_kan_payments();
    }catch(const std::exception& ex){
        std::cerr << "point_engine_tests failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "point_engine_tests passed\n";
    return EXIT_SUCCESS;
}
