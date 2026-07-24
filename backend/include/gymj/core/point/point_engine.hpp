#ifndef GYMJ_CORE_POINT_ENGINE_HPP
#define GYMJ_CORE_POINT_ENGINE_HPP

#include <vector>

#include <gymj/common/schema/tile.hpp>
#include <gymj/common/rules/rule_config.hpp>
#include <gymj/common/schema/point.hpp>

namespace gymj::rule{
    
using gymj::common::Tile;    
using gymj::common::Meld;
using gymj::common::PointRuleConfig;
using gymj::common::PointResult;
using gymj::common::RoundResult;

class PointEngine{
public:
    explicit PointEngine(PointRuleConfig config);
    PointResult calculate(const RoundResult& round_result, const Tile remaining_wall_top);
    PointResult calculate_no_winner(const RoundResult& round_result, const std::array<bool, 4>& tenpai_seats);
private:
    bool is_same_color() const;
    bool is_half_same_color() const;
    int chicken_count() const;
    PointRuleConfig config_;
};  
  
}



#endif