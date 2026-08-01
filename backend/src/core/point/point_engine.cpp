#include <utility>
#include <array>
#include <optional>

#include <gymj/core/point/point_engine.hpp>
#include <gymj/core/rules/rule_engine.hpp>

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

}

namespace gymj::rule{

using gymj::common::PlayerTileState;
using gymj::common::DashChicken;
using gymj::common::WinType;
using gymj::common::tile_index;
using gymj::common::DashChicken;

PointEngine::PointEngine(PointRuleConfig config) :config_(std::move(config)){}

PointResult PointEngine::calculate(const RoundResult& round_result, const Tile remaning_wall_top, const std::array<int, 4>& tenpai_seats){
    PointResult point_result = PointResult{};
    if(round_result.has_winner == 0){
        for(int i = 0; i < 4; i++){
            if(tenpai_seats[i] == 0){
                for(int j = 0; j < 4; j++){
                    point_result.point_to_others[i][j] += tenpai_seats[j];
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

            point_result.detail[i].point_from_chicken = chicken_count(round_result.states[i], std::make_optional(round_result.win_tile), remaning_wall_top, round_result.one_sou, round_result.eight_pin);
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
        int winner_point = calculate_winner_point(round_result.states[winner_seat], round_result.win_tile, round_result.detail);
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

int PointEngine::calculate_tile_point(const PlayerTileState& player_state, const WinDetail& detail) const{
    int total_point = 0;
    int half_same_color_cnt = half_same_color_count(player_state),
        same_color_cnt = same_color_count(player_state);
    switch (detail){
        case WinDetail::RonAddKan:
        case WinDetail::RonKanDiscard:
        case WinDetail::TsumoFromKan:
            same_color_cnt++;
            break;
        default:
            break;
    }
    if((half_same_color_cnt == 0) && (same_color_cnt == 0)){
        total_point = config_.tsumo_point;
    } else {
        total_point = config_.half_same_color_point * half_same_color_cnt + config_.same_color_point * same_color_cnt;
    }
    return total_point;
}

int PointEngine::half_same_color_count(const PlayerTileState& player_state) const{
    auto toitoiho = [&]() -> int {
        std::array<int, 27> tile_count;
        for(auto& tile : player_state.hand){
            tile_count[tile_index(tile)]++;
        }
        int pair_count = 0, meld_count = 0,
            to_pair = 0;
        for(auto cnt : tile_count){
            if (cnt == 1){
                to_pair++;
            } else if(cnt == 2){
                pair_count++;
            } else if (cnt == 3) {
                meld_count++;
            }
        }
        meld_count += player_state.melds.size();
        if((pair_count == 0) && (to_pair == 1) && (meld_count == 4)){
            return 1;
        } else if((pair_count == 2) && (meld_count == 3)){
            return 1;
        }
        return 0;
    };
    return toitoiho();
}

int PointEngine::same_color_count(const PlayerTileState& player_state) const{
    auto all_same_color = [&]() -> int {
        auto tile_type = player_state.hand[0].type;
        for(auto& tile : player_state.hand){
            if(tile_type != tile.type){
                return 0;
            }
        }
        for(auto& meld : player_state.melds){
            if(tile_type != meld.tile.type){
                return 0;
            }
        }
        return 1;
    };
    auto seven_pair = [&]() -> int {
        if(player_state.melds.size() != 0){
            return 0;
        }
        std::array<int, 27> tile_count;
        for(auto& tile : player_state.hand){
            tile_count[tile_index(tile)]++;
        }
        int pair_count = 0, to_pair = 0, dragon_count = 0, to_dragon = 0;
        for(auto cnt : tile_count){
            switch (cnt){
                case 1:
                    to_pair++;
                    break;
                case 2:
                    pair_count++;
                    break;
                case 3:
                    to_dragon++;
                    break;
                case 4:
                    dragon_count++;
                    break;
            }
        }
        if((to_pair == 1) && (dragon_count * 2 + pair_count == 6)){
            return 1 + dragon_count;
        } else if((to_dragon == 1) && (dragon_count * 2 + pair_count == 5)){
            return 1 + dragon_count + to_dragon;
        }
        return 0;
    };
    return all_same_color() + seven_pair();
}

int PointEngine::chicken_count(const PlayerTileState& player_state, const std::optional<Tile>& win_tile, const Tile& cur_round_chicken,
                               const std::optional<DashChicken>& one_sou, const std::optional<DashChicken>& eight_pin) const{
    auto is_chicken = [&](Tile tile) -> bool {
        return tile == chicken || tile == black_chicken || tile == cur_round_chicken;
    };
    int count = 0;
    for(auto& tile : player_state.river){
        if(is_chicken(tile)){
            count += config_.hand_chicken_point;
        }
    }
    if(win_tile.has_value() && is_chicken(win_tile.value())){
        count+= config_.hand_chicken_point;
    }
    if(one_sou.has_value()){}
}

}
