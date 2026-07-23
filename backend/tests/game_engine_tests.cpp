#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gymj/core/rules/game_engine.hpp>

namespace{

using gymj::common::GameRuleConfig;
using gymj::common::Meld;
using gymj::common::MeldType;
using gymj::common::Tile;
using gymj::common::TileType;
using gymj::rule::GameEngine;

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

GameEngine make_engine(bool allow_multi_ron = true){
    GameRuleConfig config{};
    config.allowMultiRon = allow_multi_ron;
    return GameEngine{config};
}

void test_pon(){
    const auto engine = make_engine();
    const std::vector<Tile> tiles{m(2), m(2), m(5), s(1)};

    require(engine.can_pon(tiles, m(2)), "can_pon should allow two matching tiles");
    require(!engine.can_pon(tiles, m(5)), "can_pon should reject one matching tile");
    require(!engine.can_pon(tiles, p(2)), "can_pon should match suit and rank");
}

void test_kan(){
    const auto engine = make_engine();

    require(engine.can_open_kan({p(7), p(7), p(7), m(1)}, p(7)),
            "can_open_kan should allow exactly three matching hand tiles");
    require(!engine.can_open_kan({p(7), p(7), m(1)}, p(7)),
            "can_open_kan should reject fewer than three matching hand tiles");
    require(!engine.can_open_kan({p(7), p(7), p(7), p(7)}, p(7)),
            "can_open_kan should reject four matching hand tiles for discard kan");

    require(engine.can_self_kan({s(3), s(3), s(3), s(3)}, s(3)),
            "can_self_kan should allow four matching tiles");
    require(!engine.can_self_kan({s(3), s(3), s(3)}, s(3)),
            "can_self_kan should reject three matching tiles");

    const std::vector<Meld> melds{
        Meld{MeldType::Pon, m(9), 1},
        Meld{MeldType::OpenKan, p(1), 2},
    };
    require(engine.can_add_kan(melds, m(9)),
            "can_add_kan should allow upgrading a matching pon meld");
    require(!engine.can_add_kan(melds, p(1)),
            "can_add_kan should reject non-pon melds");
    require(!engine.can_add_kan(melds, s(9)),
            "can_add_kan should reject missing pon melds");
}

void test_multi_ron_config(){
    require(make_engine(true).can_multi_ron(), "can_multi_ron should follow enabled config");
    require(!make_engine(false).can_multi_ron(), "can_multi_ron should follow disabled config");
}

void test_tsumo_standard_closed_shape(){
    const auto engine = make_engine();
    const std::vector<Tile> winning_tiles{
        m(1), m(2), m(3),
        m(4), m(5), m(6),
        s(1), s(2), s(3),
        p(7), p(8), p(9),
        p(5), p(5),
    };

    require(engine.can_tsumo(winning_tiles),
            "can_tsumo should allow standard four melds and one pair");
}

void test_tsumo_rejects_non_winning_shape(){
    const auto engine = make_engine();
    const std::vector<Tile> non_winning_tiles{
        m(1), m(2), m(3),
        m(4), m(5), m(6),
        s(1), s(2), s(3),
        p(7), p(8), p(9),
        p(5), p(6),
    };

    require(!engine.can_tsumo(non_winning_tiles),
            "can_tsumo should reject a hand without a pair");
}

void test_ron_adds_winning_tile(){
    const auto engine = make_engine();
    const std::vector<Tile> waiting_tiles{
        m(1), m(2), m(3),
        m(4), m(5), m(6),
        s(1), s(2), s(3),
        p(7), p(8), p(9),
        p(5),
    };

    require(engine.can_ron(waiting_tiles, p(5)),
            "can_ron should test the hand after adding the winning tile");
    require(!engine.can_ron(waiting_tiles, p(6)),
            "can_ron should reject non-winning discard tiles");
}

void test_shape_with_open_meld_count(){
    const auto engine = make_engine();
    const std::vector<Tile> closed_tiles_after_win{
        m(1), m(2), m(3),
        m(4), m(5), m(6),
        s(1), s(2), s(3),
        p(5), p(5),
    };

    require(engine.can_tsumo(closed_tiles_after_win, 1),
            "can_tsumo should account for one already fixed meld");
    require(!engine.can_tsumo(closed_tiles_after_win, 0),
            "can_tsumo should reject wrong meld_count for tile count");
}

void test_tenpai(){
    const auto engine = make_engine();
    const std::vector<Tile> tenpai_tiles{
        m(1), m(2), m(3),
        m(4), m(5), m(6),
        s(1), s(2), s(3),
        p(7), p(8), p(9),
        p(5),
    };
    const std::vector<Tile> not_tenpai_tiles{
        m(1), m(1), m(2),
        m(2), m(3), m(4),
        m(5), s(1), s(3),
        s(5), p(2), p(4),
        p(6),
    };

    require(engine.is_tenpai(tenpai_tiles),
            "is_tenpai should detect a one-pair wait");
    require(!engine.is_tenpai(not_tenpai_tiles),
            "is_tenpai should reject a hand with no winning draw");
}

void test_invalid_tile_rejected(){
    const auto engine = make_engine();
    const std::vector<Tile> invalid_tiles{
        m(1), m(2), m(3),
        m(4), m(5), m(6),
        s(1), s(2), s(3),
        p(7), p(8), p(9),
        p(5), Tile{TileType::Pin, 10},
    };

    require(!engine.can_tsumo(invalid_tiles),
            "can_tsumo should reject invalid tile ranks");
}

}

int main(){
    try{
        test_pon();
        test_kan();
        test_multi_ron_config();
        test_tsumo_standard_closed_shape();
        test_tsumo_rejects_non_winning_shape();
        test_ron_adds_winning_tile();
        test_shape_with_open_meld_count();
        test_tenpai();
        test_invalid_tile_rejected();
    }catch(const std::exception& ex){
        std::cerr << "game_engine_tests failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "game_engine_tests passed\n";
    return EXIT_SUCCESS;
}
