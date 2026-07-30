#ifndef GYMJ_CORE_POINT_ENGINE_HPP
#define GYMJ_CORE_POINT_ENGINE_HPP

#include <vector>

#include <gymj/common/schema/tile.hpp>
#include <gymj/common/rules/rule_config.hpp>
#include <gymj/common/schema/point.hpp>
#include <gymj/common/schema/round_state.hpp>
#include <gymj/common/player/player_info.hpp>

namespace gymj::rule{
    
using gymj::common::Tile;    
using gymj::common::Meld;
using gymj::common::PointRuleConfig;
using gymj::common::PointResult;
using gymj::common::RoundResult;
using gymj::common::PlayerTileState;
using gymj::common::WinDetail;

class PointEngine{
public:
    explicit PointEngine(PointRuleConfig config);
    PointResult calculate(const RoundResult& round_result, const Tile remaining_wall_top, const std::array<int, 4>& tenpai_seats);
private:
    bool is_same_color(const PlayerTileState& winner_state, const Tile& win_tile) const;
    bool is_half_same_color(const PlayerTileState& winner_state, const Tile& win_tile) const;
    int calculate_winner_point(const PlayerTileState& winner_state, const Tile& win_tile, const WinDetail& detail) const;
    int chicken_count(const PlayerTileState& player_state, const std::optional<Tile>& win_tile, const Tile& cur_round_chicken, const std::optional<DashChicken>& one_sou, const std::optional<DashChicken>& eight_pin) const;
    PointRuleConfig config_;
};  
  
}

#endif