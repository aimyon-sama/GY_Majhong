#ifndef GAME_CORE_WALL_HPP
#define GAME_CORE_WALL_HPP

#include <array>
#include <random>
#include <optional>

#include <gymj/common/schema/tile.hpp>

namespace gymj::room{

using gymj::common::Tile;

class Wall{
public:
    Wall() = default;
    void init(std::mt19937* rng);
    Tile draw_tile();
    Tile draw_kan_tile();
    bool empty() const noexcept;
private:
    struct WallStack{
        Tile upper;
        Tile lower;
        bool upper_taken = false;
        bool lower_taken = false;

        bool empty() const {
            return upper_taken && lower_taken;
        }
    };
    std::array<WallStack, 64> wall_;
    int draw_index_;
    int kan_index_;
};

}

#endif