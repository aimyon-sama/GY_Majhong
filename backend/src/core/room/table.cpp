#include <gymj/core/room/table.hpp>

namespace {

gymj::common::PlayerAction get_action(gymj::room::Command command){}

}

namespace gymj::room{
Table::Table(gymj::common::RuleConfig config)
    :config_(config), seats_(), rng_(std::random_device()()){}

TableUpdate Table::join(PlayerInfo player){
    for(int i = 0; i < 4; i++){
        if(seats_[i].is_seated == false){
            seats_[i] = SeatState{true, player, false};
            return TableUpdate{};
        }
    }
    return TableUpdate{};
}

TableUpdate Table::ready(PlayerInfo player){
    for(int i = 0; i < 4; i++){
        if(seats_[i].player == player){
            seats_[i].is_ready = true;
            return TableUpdate{};
        }
    }
    return TableUpdate{};
}

TableUpdate Table::start(){
    std::array<PlayerInfo, 4> players{};
    for(int i = 0; i < 4; i++){
        players[i] = seats_[i].player;
    }
    round_ = std::make_unique<gymj::room::Round>(config_, players, &rng_);
}

TableUpdate Table::disconnect(PlayerInfo player){
    for(int i = 0; i < 4; i++){
        if(seats_[i].player == player){
            return TableUpdate{};
        }
    }
    return TableUpdate{};
}

TableUpdate Table::submit(PlayerInfo player, const Command command){
    if(round_ == nullptr){

    }
    int seat;
    for(seat = 0; seat < 4; seat++){
        if(seats_[seat].player == player){
            break;
        }
    }
    auto transition = round_ -> submit_action(seat, get_action(command));
    return TableUpdate{};
}

TableUpdate Table::tick(){

}

gymj::common::PlayerTileState Table::snapshot(PlayerInfo player) const{

}

TableUpdate Table::consume(common::RoundTransition transition){}

TableUpdate Table::advance(){}

}