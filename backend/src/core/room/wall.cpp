#include <gymj/core/room/wall.hpp>

#include <gymj/common/schema/tile.hpp>

#include <array>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace gymj::room{

void Wall::init(std::mt19937* rng){
    std::vector<Tile> tiles;
    tiles.reserve(tile_count);

    for(int tile_num = 0; tile_num < gymj::common::tileKindCount; tile_num++){
        for(int count = 0; count < 4; count++){
            tiles.push_back(gymj::common::tile_from_index(tile_num));
        }
    }
    std::shuffle(tiles.begin(), tiles.end(), *rng);

    std::array<Tile, tile_count> physical_tiles{};
    std::copy(tiles.begin(), tiles.end(), physical_tiles.begin());
    init(physical_tiles);
}

void Wall::init(const std::array<Tile, tile_count>& tiles){
    draw_index_ = 0;
    kan_index_ = stack_count - 1;
    remaining_ = tile_count;

    for(int i = 0; i < stack_count; i++){
        wall_[i] = WallStack{tiles[i * 2], tiles[i * 2 + 1], false, false};
    }
}

bool Wall::empty() const noexcept{
    return remaining_ == 0;
}

int Wall::remaining() const noexcept{
    return remaining_;
}

Tile Wall::draw_tile(){
    auto& stack = wall_[draw_index_];
    if(stack.upper_taken == false){
        stack.upper_taken = true;
        --remaining_;
        return stack.upper;
    }

    stack.lower_taken = true;
    --remaining_;
    ++draw_index_;
    return stack.lower;
}

Tile Wall::draw_kan_tile(){
    auto& stack = wall_[kan_index_];
    if(stack.upper_taken == false){
        stack.upper_taken = true;
        --remaining_;
        return stack.upper;
    }

    stack.lower_taken = true;
    --remaining_;
    --kan_index_;
    return stack.lower;
}

}
