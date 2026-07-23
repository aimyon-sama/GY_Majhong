#ifndef GYMJ_CORE_POINT_ENGINE_HPP
#define GYMJ_CORE_POINT_ENGINE_HPP

#include <vector>

#include <gymj/common/schema/tile.hpp>
#include <gymj/common/rules/rule_config.hpp>

namespace gymj::rule{
    
using gymj::common::Tile;    
using gymj::common::Meld;
using gymj::common::PointRuleConfig;

class PointEngine{
public:
    explicit PointEngine(PointRuleConfig config);
    
private:
    PointRuleConfig config_;
};  
  
}



#endif