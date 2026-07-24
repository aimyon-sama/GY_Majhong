#include <utility>
#include <array>

#include <gymj/core/point/point_engine.hpp>

namespace {

using gymj::common::Tile;
using gymj::common::PlayerTileState;

constexpr Tile chicken = Tile{gymj::common::TileType::Sou, 1};
constexpr Tile black_chicken = Tile{gymj::common::TileType::Pin, 8};

}

namespace gymj::rule{

PointEngine::PointEngine(PointRuleConfig config) :config_(std::move(config)){}

PointResult PointEngine::calculate(const RoundResult& round_result, const Tile remaning_wall_top){
    
}

PointResult PointEngine::calculate_no_winner(const RoundResult& round_result, const std::array<bool, 4>& tenpai_seats){
    for(int i = 0; i < 4; i++){
        if(tenpai_seats[i] == false){
            
        }
    }
}

}