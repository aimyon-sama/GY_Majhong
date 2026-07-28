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
            result.delta_result[i] += result.point_to_others[j][i];
            result.delta_result[i] -= result.point_to_others[i][j];
        }
    }
}

int calculate_winner_point(){}

}

namespace gymj::rule{

using gymj::common::PlayerTileState;
using gymj::common::DashChicken;
using gymj::common::WinType;

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
        //calculate point of every seat
        for(int i = 0; i < 4; i++){
            if(round_result.discarder_seat == i && round_result.detail == gymj::common::WinDetail::RonKanDiscard){
                continue;
            }
            int cur_point = 0;

            point_result.detail[i].point_from_chicken = chicken_count();
            cur_point += point_result.detail[i].point_from_chicken;

            for(auto& meld : round_result.states[i].melds){
                if(meld.type == gymj::common::MeldType::AddKan || meld.type == gymj::common::MeldType::OpenKan){
                    point_result.detail[i].point_from_kan += config_.kan_point;
                }
            }
            cur_point += point_result.detail[i].point_from_kan;

            for(int j = 0; j < 4; j++){
                if(j != i){
                    point_result.point_to_others[j][i] += cur_point;
                }
            }
            point_result.detail[i].total_point += cur_point;
        }
        //calculate point of winner
        int winner_seat = round_result.winner_seat;
        int winner_point = calculate_winner_point();
        switch (round_result.win_type){
            case WinType::Tsumo:
                for(int i = 0; i < 4; i++){
                    if(i != winner_seat){
                        point_result.point_to_others[i][winner_seat] += winner_point;
                    }
                }
                break;
            case WinType::Ron: {
                int discarder_seat = round_result.discarder_seat;
                point_result.point_to_others[discarder_seat][winner_seat] += winner_point;
                break;
            }
            default:
                return PointResult{};
        }
    }
    return point_result;
}



}
