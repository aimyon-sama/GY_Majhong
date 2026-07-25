#include <utility>
#include <array>

#include <gymj/core/point/point_engine.hpp>


namespace {

using gymj::common::Tile;

constexpr Tile chicken = Tile{gymj::common::TileType::Sou, 1};
constexpr Tile black_chicken = Tile{gymj::common::TileType::Pin, 8};

void calculate_delta(gymj::common::PointResult& result){
    for(int i = 0; i < 4;i++){
        result.delta_result[i] = 0;
        for(int j = 0; j <4; j++){
            result.delta_result[i] -= result.point_to_others[i][j];
        }
    }
}

}

namespace gymj::rule{

using gymj::common::PlayerTileState;
using gymj::common::DashChicken;

PointEngine::PointEngine(PointRuleConfig config) :config_(std::move(config)){}

PointResult PointEngine::calculate(const RoundResult& round_result, const Tile remaning_wall_top, const std::array<int, 4>& tenpai_seats){
    PointResult point_result = PointResult{};
    if(round_result.has_winner == 0){
        for(int i = 0; i < 4; i++){
            if(tenpai_seats[i] == 0){
                for(int j = 0; j < 4; j++){
                    point_result.point_to_others[i][j] += tenpai_seats[j];
                    point_result.point_to_others[j][i] -= tenpai_seats[j];
                }
            }
        }
        calculate_delta(point_result);
        for(int i = 0; i <4; i++){
            point_result.detail[i] = gymj::common::PointDetail{point_result.delta_result[i], 0, 0, 0, point_result.delta_result[i]};
        }
    } else {
        
    }
    return point_result;
}


}