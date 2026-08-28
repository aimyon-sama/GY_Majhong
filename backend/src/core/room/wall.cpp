#include <gymj/core/room/wall.hpp>

#include <gymj/common/schema/tile.hpp>

#include <vector>
#include <algorithm>

namespace gymj::room{

void Wall::init(std::mt19937* rng){
    draw_index_ = 0;
    kan_index_ = 63;

    std::vector<Tile> tiles;
    tiles.reserve(128);
    for(int tile_num = 0; tile_num <= 27; tile_num++){
        for(int count = 0; count < 4; count++){
            tiles.push_back(gymj::common::tile_from_index(tile_num));
        }
    }
    std::shuffle(tiles.begin(), tiles.end(), rng);
    for(int i = 0; i < 64; i++){
        wall_[i] = WallStack{tiles[i * 2], tiles[i * 2 + 1], false, false};
    }
}

bool Wall::empty() const noexcept{
    return (draw_index_ == kan_index_) && wall_[draw_index_].empty();
}

Tile Wall::draw_tile(){
    auto stack = wall_[draw_index_];
    if(stack.upper_taken == false){
        stack.upper_taken = true;
        return stack.upper;
    }
    stack.lower_taken = true;
    draw_index_++;
    return stack.lower;
}

Tile Wall::draw_kan_tile(){
    auto stack = wall_[kan_index_];
    if(stack.upper_taken == false){
        stack.upper_taken = true;
        return stack.upper;
    }
    stack.lower_taken = true;
    kan_index_++;
    return stack.lower;
}
    