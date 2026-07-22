#ifndef GYMJ_CORE_GAME_ENGINE_HPP
#define GYMJ_CORE_GAME_ENGINE_HPP

#include <vector>

#include <gymj/common/schema/tile.hpp>
#include <gymj/common/rules/rule_config.hpp>

namespace gymj::rule{
    
using gymj::common::Tile;    
using gymj::common::Meld;
using gymj::common::GameRuleConfig;

class GameEngine{
public:
    explicit GameEngine(GameRuleConfig config);
    bool can_pon(const std::vector<Tile>& tiles, Tile to_pon) const;
    bool can_open_kan(const std::vector<Tile>& tiles, Tile to_kan) const;
    bool can_add_kan(const std::vector<Meld>& melds, Tile to_kan) const;
    bool can_self_kan(const std::vector<Tile>& tiles, Tile to_kan) const;
    bool can_tsumo(const std::vector<Tile>& tiles, int meld_count = 0) const;
    bool can_ron(const std::vector<Tile>& tiles, Tile to_ron, int meld_count = 0) const;
    bool can_multi_ron() const;
    bool is_tenpai(const std::vector<Tile>& tiles, int meld_count = 0) const;
private:
    bool is_agari_shape(const std::vector<Tile>& tiles, int meld_count = 0) const;
    GameRuleConfig config_;
};  
  
}



#endif