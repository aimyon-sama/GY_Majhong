#ifndef GYMJ_COMMON_POINT_HPP
#define GYMJ_COMMON_POINT_HPP

#include <array>
#include <optional>

#include <gymj/common/schema/tile.hpp>
#include <gymj/common/player/player_info.hpp>

namespace gymj::common{

struct PointDetail{
    int total_point = 0;
    int point_from_chicken = 0;
    int point_from_kan = 0;
    int point_from_agari = 0;
    int point_from_tenpai = 0; // in no winner round
};

struct PointResult{
    std::array<std::array<int, 4>, 4> point_to_others;
    std::array<int, 4> delta_result;
    std::array<PointDetail, 4> detail;
};

}



#endif