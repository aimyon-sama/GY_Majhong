#ifndef GYMJ_CORE_ROUND_HPP
#define GYMJ_CORE_ROUND_HPP

#include <vector>

#include <gymj/common/schema/tile.hpp>
#include <gymj/common/schema/round_state.hpp>
#include <gymj/core/rules/rule_engine.hpp>

namespace gymj::room{

using gymj::common::RoundState;
using gymj::common::RoundStage;
using gymj::rule::RuleEngine;
using gymj::common::Tile;

class Round{
public:
    Round();
private:
    RoundState state_;
    RuleEngine rule_engine_;
    int cur_player;
    int dealer_seat;

    std::vector<Tile> wall_;
    int draw_index;
    int kan_index;
    Tile draw_tile();
    Tile draw_kan_tile();
};
    
}

#endif