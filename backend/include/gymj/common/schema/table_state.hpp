#ifndef GYMJ_COMMON_TABLE_STATE_HPP
#define GYMJ_COMMON_TABLE_STATE_HPP

namespace gymj::common{

struct Command{
    std::uint64_t round_id = 0;
    std::uint64_t request_id = 0;
    std::uint64_t prompt_id = 0;
    gymj::common::PlayerAction action;
};

struct TableUpdate{};

}

#endif