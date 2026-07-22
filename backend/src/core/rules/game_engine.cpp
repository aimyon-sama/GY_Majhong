#include <array>
#include <utility>

#include <gymj/core/rules/game_engine.hpp>

namespace gymj::rule{

namespace{

bool can_take_sequence(int index, const std::array<int, gymj::common::tileKindCount>& counts){
    const int rank_index = index % gymj::common::ranksPerSuit;
    return rank_index <= 6 && counts[index] > 0 && counts[index + 1] > 0 && counts[index + 2] > 0;
}

bool can_form_melds(std::array<int, gymj::common::tileKindCount>& counts, int melds_left){
    if(melds_left == 0){
        for(int count:counts){
            if(count != 0){
                return false;
            }
        }
        return true;
    }

    int first = -1;
    for(int i = 0; i < gymj::common::tileKindCount; ++i){
        if(counts[i] > 0){
            first = i;
            break;
        }
    }

    if(first < 0){
        return false;
    }

    if(counts[first] >= 3){
        counts[first] -= 3;
        if(can_form_melds(counts, melds_left - 1)){
            counts[first] += 3;
            return true;
        }
        counts[first] += 3;
    }

    if(can_take_sequence(first, counts)){
        --counts[first];
        --counts[first + 1];
        --counts[first + 2];
        if(can_form_melds(counts, melds_left - 1)){
            ++counts[first];
            ++counts[first + 1];
            ++counts[first + 2];
            return true;
        }
        ++counts[first];
        ++counts[first + 1];
        ++counts[first + 2];
    }

    return false;
}

}

GameEngine::GameEngine(GameRuleConfig config)
    :config_(std::move(config)){}

bool GameEngine::can_pon(const std::vector<Tile>& tiles, Tile to_pon) const{
    int cnt = 0;
    for(auto& t:tiles){
        if(t == to_pon){
            cnt++;
        }
    }
    return cnt >= 2;
}

bool GameEngine::can_open_kan(const std::vector<Tile>& tiles, Tile to_kan) const{
    int cnt = 0;
    for(auto& t:tiles){
        if(t == to_kan){
            cnt++;
        }
    }
    return cnt == 3;
}

bool GameEngine::can_add_kan(const std::vector<Meld>& melds, Tile to_kan) const{
    for(auto& m:melds){
        if(m.type == gymj::common::MeldType::Pon && m.tile == to_kan){
            return true;
        }
    }
    return false;
}

bool GameEngine::can_self_kan(const std::vector<Tile>& tiles, Tile to_kan) const{
    int cnt = 0;
    for(auto& t:tiles){
        if(t == to_kan){
            cnt++;
        }
    }
    return cnt == 4;
}

bool GameEngine::can_tsumo(const std::vector<Tile>& tiles, int meld_count) const{
    return is_agari_shape(tiles, meld_count);
}

bool GameEngine::can_ron(const std::vector<Tile>& tiles, Tile to_ron, int meld_count) const{
    auto tmp = tiles;
    tmp.push_back(to_ron);
    return is_agari_shape(tmp, meld_count);
}

bool GameEngine::can_multi_ron() const{
    return config_.allowMultiRon;
}

bool GameEngine::is_tenpai(const std::vector<Tile>& tiles, int meld_count) const{
    std::array<int, gymj::common::tileKindCount> counts{};
    for(const auto& tile:tiles){
        const int index = gymj::common::tile_index(tile);
        if(index < 0){
            return false;
        }
        ++counts[index];
    }

    for(int i = 0; i < gymj::common::tileKindCount; ++i){
        if(counts[i] >= 4){
            continue;
        }

        auto tmp = tiles;
        tmp.push_back(gymj::common::tile_from_index(i));
        if(is_agari_shape(tmp, meld_count)){
            return true;
        }
    }

    return false;
}

bool GameEngine::is_agari_shape(const std::vector<Tile>& tiles, int meld_count) const{
    if(meld_count < 0 || meld_count > gymj::common::completeMeldCount){
        return false;
    }

    const auto total_tile_count = static_cast<int>(tiles.size()) + meld_count * gymj::common::meldTileCount;
    if(total_tile_count != gymj::common::handTileCount){
        return false;
    }

    const int needed_melds = gymj::common::completeMeldCount - meld_count;
    if(static_cast<int>(tiles.size()) != needed_melds * gymj::common::meldTileCount + 2){
        return false;
    }

    std::array<int, gymj::common::tileKindCount> counts{};
    for(const auto& tile:tiles){
        const int index = gymj::common::tile_index(tile);
        if(index < 0){
            return false;
        }

        ++counts[index];
        if(counts[index] > 4){
            return false;
        }
    }

    for(int pair_index = 0; pair_index < gymj::common::tileKindCount; ++pair_index){
        if(counts[pair_index] < 2){
            continue;
        }

        counts[pair_index] -= 2;
        if(can_form_melds(counts, needed_melds)){
            counts[pair_index] += 2;
            return true;
        }
        counts[pair_index] += 2;
    }

    return false;
}

}
