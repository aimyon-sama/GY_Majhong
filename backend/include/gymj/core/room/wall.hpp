#ifndef GAME_CORE_WALL_HPP
#define GAME_CORE_WALL_HPP

#include <array>
#include <random>

#include <gymj/common/schema/tile.hpp>

namespace gymj::room{

using gymj::common::Tile;

class Wall{
public:
    static constexpr int stack_count = 54;
    static constexpr int tile_count = stack_count * 2;

    Wall() = default;

    void init(std::mt19937* rng);
    void init(const std::array<Tile, tile_count>& tiles);
    Tile draw_tile();
    Tile draw_kan_tile();
    bool empty() const noexcept;
    int remaining() const noexcept;
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
    std::array<WallStack, stack_count> wall_{};
    int draw_index_ = 0;
    int kan_index_ = stack_count - 1;
    int remaining_ = 0;
};

}

#endif
