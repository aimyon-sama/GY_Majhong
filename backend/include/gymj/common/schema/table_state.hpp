#ifndef GYMJ_COMMON_TABLE_STATE_HPP
#define GYMJ_COMMON_TABLE_STATE_HPP

#include <chrono>
#include <gymj/common/schema/round_state.hpp>

namespace gymj::common{

// 单调时钟不受系统时间校准影响，用于计算操作超时。
using TableClock = std::chrono::steady_clock;

// 同一玩家在同一小局内递增 request_id；多人响应共用同一个 prompt_id。
struct Command{
    std::uint64_t round_id = 0;
    std::uint64_t request_id = 0;
    std::uint64_t prompt_id = 0;
    gymj::common::PlayerAction action{};
};

enum class TableStage { WaitingReady, Playing, RoundFinished };

// 庄家沿用配置；指定种子可用于确定性测试，不指定时使用随机种子。
struct TableOptions {
    int dealer_seat = 0;
    int action_timeout_ms = 8000;
    std::optional<std::uint64_t> seed;
};

// 所有玩家可见的座位信息；手牌仅公开数量，暗杠牌面由 Table 过滤。
struct SeatView {
    bool occupied = false;
    bool connected = false;
    bool ready = false;
    std::string player_name;
    int hand_size = 0;
    std::vector<Tile> river;
    std::vector<Meld> melds;
};

// 单个玩家可见的完整快照，不包含后端身份 ID，也不暴露其他人的局内手牌。
struct PlayerView {
    int self_seat = -1;
    TableStage table_stage = TableStage::WaitingReady;
    RoundStage round_stage = RoundStage::NotActive;
    std::uint64_t table_seq = 0;
    std::uint64_t round_id = 0;
    std::uint64_t round_seq = 0;
    std::uint64_t prompt_id = 0;
    int acting_player = -1;
    std::uint64_t last_request_id = 0; // 重连后从大于此值的请求编号继续。
    int dealer_seat = 0;
    int tiles_remaining = 0;
    std::array<SeatView, 4> seats;
    PlayerTileState own_tiles;
    std::vector<PlayerAction> available_actions;
    // 网络层应转换为剩余毫秒数，不能当作现实时间戳发送。
    std::optional<TableClock::time_point> deadline;
    std::array<std::int64_t, 4> total_points{};
    std::optional<DashChicken> one_sou;
    std::optional<DashChicken> eight_pin;
    std::optional<PlayerAction> pending_action;
    std::optional<RoundResult> round_result; // 仅在小局结束后公开完整结局。
    std::optional<PointResult> point_result;
};

struct Delivery {
    std::uint64_t recipient_id = 0; // 仅供后端查找接收会话，不发给前端。
    PlayerView view;
    std::vector<RoundEvent> events;
};

// 一次牌桌调用的结果。网络层按接收人依次发送 view/events，并单独返回请求回执。
// view 是对应时刻的完整状态，events 用于动画，不应再重复修改同一份快照。
struct TableUpdate {
    bool accepted = false;
    std::string error;
    bool duplicate = false; // 重复请求仅返回原回执，不再次生成消息或执行动作。
    std::uint64_t request_id = 0;
    std::uint64_t table_seq = 0;
    std::optional<PlayerInfo> assigned_player; // 入座成功后绑定到后端会话，不整体序列化给前端。
    std::vector<Delivery> deliveries;
};

}

#endif
