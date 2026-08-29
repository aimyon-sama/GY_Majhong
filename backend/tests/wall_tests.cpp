#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include <gymj/core/room/wall.hpp>

namespace{

using gymj::common::Tile;
using gymj::common::TileType;
using gymj::room::Wall;

Tile numbered_tile(int number){
    return Tile{TileType::Man, static_cast<std::uint8_t>(number)};
}

std::array<Tile, Wall::tile_count> ordered_tiles(){
    std::array<Tile, Wall::tile_count> tiles{};
    for(int i = 0; i < Wall::tile_count; ++i){
        tiles[i] = numbered_tile(i + 1);
    }
    return tiles;
}

void require(bool condition, const std::string& message){
    if(!condition){
        throw std::runtime_error(message);
    }
}

void require_tile(Tile actual, int expected_number, const std::string& message){
    require(actual == numbered_tile(expected_number), message);
}

void test_head_draw_uses_stack_order(){
    Wall wall;
    wall.init(ordered_tiles());

    require(!wall.empty(), "newly initialized wall should not be empty");
    require(wall.remaining() == Wall::tile_count, "newly initialized wall should expose all tiles");

    require_tile(wall.draw_tile(), 1, "first head draw should take stack 0 upper tile");
    require_tile(wall.draw_tile(), 2, "second head draw should take stack 0 lower tile");
    require_tile(wall.draw_tile(), 3, "third head draw should take stack 1 upper tile");
    require_tile(wall.draw_tile(), 4, "fourth head draw should take stack 1 lower tile");
    require(wall.remaining() == Wall::tile_count - 4, "head draws should reduce remaining count");
}

void test_kan_draw_uses_tail_stack_order(){
    Wall wall;
    wall.init(ordered_tiles());

    require_tile(wall.draw_kan_tile(), Wall::tile_count - 1, "first kan draw should take last stack upper tile");
    require_tile(wall.draw_kan_tile(), Wall::tile_count, "second kan draw should take last stack lower tile");
    require_tile(wall.draw_kan_tile(), Wall::tile_count - 3, "third kan draw should take previous stack upper tile");
    require_tile(wall.draw_kan_tile(), Wall::tile_count - 2, "fourth kan draw should take previous stack lower tile");
    require(wall.remaining() == Wall::tile_count - 4, "kan draws should reduce remaining count");
}

void test_head_and_tail_draws_do_not_overlap(){
    Wall wall;
    wall.init(ordered_tiles());

    std::array<bool, Wall::tile_count + 1> seen{};
    for(int i = 0; i < Wall::stack_count / 2; ++i){
        const auto head_tile = wall.draw_tile();
        const auto tail_tile = wall.draw_kan_tile();

        const int head_number = head_tile.rank;
        const int tail_number = tail_tile.rank;
        require(head_number >= 1 && head_number <= Wall::tile_count, "head draw should return a tracked tile");
        require(tail_number >= 1 && tail_number <= Wall::tile_count, "kan draw should return a tracked tile");
        require(!seen[head_number], "head draw should not repeat a tile");
        require(!seen[tail_number], "kan draw should not repeat a tile");

        seen[head_number] = true;
        seen[tail_number] = true;
    }

    require(wall.remaining() == Wall::tile_count - Wall::stack_count, "alternating draws should update remaining count");
}

void test_empty_wall_after_all_tiles_are_drawn(){
    Wall wall;
    wall.init(ordered_tiles());

    for(int i = 0; i < Wall::tile_count; ++i){
        wall.draw_tile();
    }

    require(wall.empty(), "wall should be empty after all tiles are drawn");
    require(wall.remaining() == 0, "empty wall should report zero remaining tiles");
}

}

int main(){
    try{
        test_head_draw_uses_stack_order();
        test_kan_draw_uses_tail_stack_order();
        test_head_and_tail_draws_do_not_overlap();
        test_empty_wall_after_all_tiles_are_drawn();
    }catch(const std::exception& ex){
        std::cerr << "wall_tests failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "wall_tests passed\n";
    return EXIT_SUCCESS;
}
