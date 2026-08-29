#include <utility>
#include <array>
#include <optional>

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

}

namespace gymj::rule{

using gymj::common::PlayerTileState;
using gymj::common::DashChicken;
using gymj::common::WinType;
using gymj::common::tile_index;
using gymj::common::DashChicken;
using gymj::common::MeldType;

PointEngine::PointEngine(PointRuleConfig config) :config_(std::move(config)){}

PointResult PointEngine::calculate(const RoundResult& round_result, const Tile round_chicken, const std::array<bool, 4>& tenpai_seats){
    PointResult point_result = PointResult{};
    if(round_result.has_winner == 0){
        for(int i = 0; i < 4; i++){
            if(tenpai_seats[i] == 0){
                for(int j = 0; j < 4; j++){
                    if(tenpai_seats[j] == true){
                        point_result.point_to_others[i][j] += calculate_tile_point(round_result.states[j], round_result.detail);
                    }
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

            auto is_chicken = [](Tile tile) -> bool {
                return (tile == chicken) || (tile == black_chicken);
            };
            auto is_unclaimed_dash_from = [](const std::optional<DashChicken>& dash_chicken, int seat) -> bool {
                return dash_chicken.has_value()
                    && dash_chicken->discarded_by == seat
                    && dash_chicken->claimed_by < 0;
            };
            if(tenpai_seats[i] == false){
                // be responsible of chicken
                int responsible_chicken = 0;
                for(auto& t : round_result.states[i].hand){
                    if(is_chicken(t)){
                        responsible_chicken += config_.hand_chicken_point;
                    }
                }
                for(auto& t : round_result.states[i].river){
                    if(is_chicken(t)){
                        responsible_chicken += config_.hand_chicken_point;
                    }
                }
                for(auto& m : round_result.states[i].melds){
                    int meld_num = 0;
                    switch (m.type) {
                    case MeldType::Pon:
                        meld_num = 3;
                        break;
                    default:
                        meld_num = 4;
                    }
                    if(is_chicken(m.tile)){
                        responsible_chicken += config_.hand_chicken_point * meld_num;
                    }
                }
                if(is_unclaimed_dash_from(round_result.one_sou, i)){
                    responsible_chicken += config_.dash_chicken_point - config_.hand_chicken_point;
                }
                if(is_unclaimed_dash_from(round_result.eight_pin, i)){
                    responsible_chicken += config_.dash_chicken_point - config_.hand_chicken_point;
                }
                for(int j = 0; j < 4; j++){
                    if(i == j || tenpai_seats[j] == false) {
                        continue;
                    }
                    point_result.point_to_others[i][j] += responsible_chicken;
                    point_result.detail[i].point_from_chicken -= responsible_chicken;
                }
                // be responsible of kan
                for(auto& meld : round_result.states[i].melds){
                    if(meld.type == gymj::common::MeldType::OpenKan){
                        int discarder_seat = meld.from_seat;
                        point_result.point_to_others[i][discarder_seat] += config_.kan_point;
                        point_result.detail[i].point_from_kan -= config_.kan_point;
                    } else if(meld.type == gymj::common::MeldType::AddKan || meld.type == gymj::common::MeldType::SelfKan){
                        for(int j = 0; j < 4; j++){
                            if(i == j || tenpai_seats[j] == false){
                                continue;
                            }
                            point_result.point_to_others[i][j] += config_.kan_point;
                            point_result.detail[i].point_from_kan -= config_.kan_point;
                        }
                    }
                }
            } else {
                int get_chicken = 0;
                if(round_result.winner_seat == i && (is_chicken(round_result.win_tile) || round_result.win_tile == round_chicken)){
                    get_chicken += config_.hand_chicken_point;
                }
                for(auto& t : round_result.states[i].hand){
                    if(is_chicken(t) || t == round_chicken){
                        get_chicken += config_.hand_chicken_point;
                    }
                }
                for(auto& t : round_result.states[i].river){
                    if(is_chicken(t) || t == round_chicken){
                        get_chicken += config_.hand_chicken_point;
                    }
                }
                for(auto& m : round_result.states[i].melds){
                    int meld_num = 0;
                    switch (m.type) {
                    case MeldType::Pon:
                        meld_num = 3;
                        break;
                    default:
                        meld_num = 4;
                    }
                    if(is_chicken(m.tile) || m.tile == round_chicken){
                        get_chicken += config_.hand_chicken_point * meld_num;
                    }
                }
                if(is_unclaimed_dash_from(round_result.one_sou, i)){
                    get_chicken += config_.dash_chicken_point - config_.hand_chicken_point;
                }
                if(is_unclaimed_dash_from(round_result.eight_pin, i)){
                    get_chicken += config_.dash_chicken_point - config_.hand_chicken_point;
                }
                for(int j = 0; j < 4; j++){
                    if(i == j) {
                        continue;
                    }
                    point_result.point_to_others[j][i] += get_chicken;
                    point_result.detail[i].point_from_chicken += get_chicken;
                }
                // be responsible of kan
                for(auto& meld : round_result.states[i].melds){
                    if(meld.type == gymj::common::MeldType::OpenKan){
                        int discarder_seat = meld.from_seat;
                        point_result.point_to_others[discarder_seat][i] += config_.kan_point;
                        point_result.detail[i].point_from_kan += config_.kan_point;
                    } else if(meld.type == gymj::common::MeldType::AddKan || meld.type == gymj::common::MeldType::SelfKan){
                        for(int j = 0; j < 4; j++){
                            if(i == j){
                                continue;
                            }
                            point_result.point_to_others[j][i] += config_.kan_point;
                            point_result.detail[i].point_from_kan += config_.kan_point;
                        }
                    }
                }
            }
        }
        auto apply_claimed_dash_chicken = [&](const std::optional<DashChicken>& dash_chicken){
            if(!dash_chicken.has_value() || dash_chicken->claimed_by < 0){
                return;
            }
            const int payer = dash_chicken->discarded_by;
            const int receiver = dash_chicken->claimed_by;
            if(payer < 0 || receiver < 0 || payer == receiver){
                return;
            }
            const int point = config_.dash_chicken_point - config_.hand_chicken_point;
            point_result.point_to_others[payer][receiver] += point;
            point_result.detail[payer].point_from_chicken -= point;
            point_result.detail[receiver].point_from_chicken += point;
        };
        apply_claimed_dash_chicken(round_result.one_sou);
        apply_claimed_dash_chicken(round_result.eight_pin);

        //calculate point of winner
        int winner_seat = round_result.winner_seat;
        int winner_point = calculate_tile_point(round_result.states[winner_seat], round_result.detail);
        auto no_chicken_no_kan = [&]() -> int {
            if(round_result.detail == gymj::common::WinDetail::TsumoFromKan ||
               round_result.detail == gymj::common::WinDetail::RonKanDiscard ||
               round_result.detail == gymj::common::WinDetail::RonAddKan){
                return 0;
            }
            for(auto tile : round_result.states[winner_seat].hand){
                if(tile == chicken || tile == black_chicken || tile == round_chicken){
                    return 0;
                }
            }
            for(auto meld : round_result.states[winner_seat].melds){
                if(meld.tile == chicken || meld.tile == black_chicken || meld.tile == round_chicken){
                    return 0;
                }
                if(meld.type != MeldType::Pon){
                    return 0;
                }
            }
            return 1;
        };
        winner_point += config_.allow_no_chicken_no_kan && no_chicken_no_kan()? config_.half_same_color_point : 0; 
        switch (round_result.win_type){
            case WinType::Tsumo:
                for(int i = 0; i < 4; i++){
                    if(i != winner_seat){
                        point_result.point_to_others[i][winner_seat] += winner_point;
                        point_result.detail[winner_seat].point_from_agari += winner_point;
                    }
                }
                break;
            case WinType::Ron: {
                int discarder_seat = round_result.discarder_seat;
                point_result.point_to_others[discarder_seat][winner_seat] += winner_point;
                point_result.detail[winner_seat].point_from_agari += winner_point;
                break;
            }
            default:
                return PointResult{};
        }

        //conclusion
        calculate_delta(point_result);
        for(int i = 0; i <4; i++){
            point_result.detail[i].total_point = point_result.delta_result[i];
        }
    }
    return point_result;
}

bool PointEngine::can_simple_ron(const PlayerTileState& player_state) const{
    return (same_color_count(player_state) > 0) || (half_same_color_count(player_state) > 0);
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
        std::array<int, 27> tile_count{};
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
        } else if((pair_count == 1) && (meld_count == 4)){
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
        std::array<int, 27> tile_count{};
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
        } else if(dragon_count * 2 + pair_count == 7){
            return 1 + dragon_count + to_dragon;
        }
        return 0;
    };
    return all_same_color() + seven_pair();
}

}
