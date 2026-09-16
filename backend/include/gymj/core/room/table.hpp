#ifndef GYMJ_CORE_TABLE_HPP
#define GYMJ_CORE_TABLE_HPP

#include <array>
#include <memory>
#include <random>

#include <gymj/common/schema/table_state.hpp>
#include <gymj/core/room/round.hpp>
#include <gymj/common/player/player_info.hpp>

namespace gymj::room{

using gymj::common::TableUpdate;
using gymj::common::PlayerInfo;

class Table {
public:
    Table(gymj::common::RuleConfig config);

    TableUpdate join(PlayerInfo player);
    TableUpdate ready(PlayerInfo player);
    TableUpdate start();
    TableUpdate disconnect(PlayerInfo player);

    TableUpdate submit(PlayerInfo player, const gymj::common::Command command);
    TableUpdate tick();
    gymj::common::PlayerTileState snapshot(PlayerInfo player) const;

private:
    TableUpdate consume(common::RoundTransition transition);
    TableUpdate advance();

    gymj::common::RuleConfig config_;
    struct SeatState{
        bool is_seated = false;
        PlayerInfo player;
        bool is_ready = false;
    };
    std::array<SeatState, 4> seats_;
    std::unique_ptr<gymj::room::Round> round_;
    std::mt19937 rng_;
    // ReplayRecorder& recorder_;
};

}



#endif