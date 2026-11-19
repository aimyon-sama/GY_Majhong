#ifndef GYMJ_COMMON_POINT_HPP
#define GYMJ_COMMON_POINT_HPP

#include <array>
#include <optional>

#include <gymj/common/schema/tile.hpp>
#include <gymj/common/player/player_info.hpp>

namespace gymj::common{

// 每个分类是该座位的小局净额：收到为正，付出为负。
// 四人的同一分类之和、以及各座位 total_point 之和都应为 0。
struct PointDetail{
    int total_point = 0;
    int point_from_chicken = 0;
    int point_from_kan = 0;
    int point_from_agari = 0;
    int point_from_tenpai = 0; // 荒牌时的听牌支付
};

struct PointResult{
    std::array<std::array<int, 4>, 4> point_to_others;
    std::array<int, 4> delta_result;
    std::array<PointDetail, 4> detail;
};

}



#endif