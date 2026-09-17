#ifndef GYMJ_CORE_TABLE_HPP
#define GYMJ_CORE_TABLE_HPP

#include <array>
#include <memory>
#include <random>
#include <deque>

#include <gymj/common/schema/table_state.hpp>
#include <gymj/core/room/round.hpp>
#include <gymj/common/player/player_info.hpp>
#include <gymj/storage/replay/replay.hpp>

namespace gymj::room{

using gymj::common::TableUpdate;
using gymj::common::PlayerInfo;

// 同一牌桌的操作、断线通知和定时器必须串行调用。
// PlayerInfo 来自已验证的后端会话，不能直接信任客户端提交的身份。
class Table {
public:
    using Clock = common::TableClock;
    using TimePoint = Clock::time_point;

    explicit Table(common::RuleConfig config, common::TableOptions options = {});

    // id 为 0 表示新玩家；已入座 ID 表示重复入座或断线重连。
    TableUpdate join(PlayerInfo player);
    TableUpdate ready(PlayerInfo player);
    TableUpdate start();
    TableUpdate start(TimePoint now); // 显式传入时间，便于测试倒计时边界。
    // 固定牌山入口用于构造可重复的规则测试。
    TableUpdate start(const std::array<common::Tile, Wall::tile_count>& tiles,
                      TimePoint now = Clock::now());
    TableUpdate disconnect(PlayerInfo player); // 保留座位和原有截止时间。

    TableUpdate submit(PlayerInfo player, const gymj::common::Command command);
    TableUpdate submit(PlayerInfo player, common::Command command, TimePoint now);
    TableUpdate tick();
    TableUpdate tick(TimePoint now);
    // 未入座身份返回空值，合法身份只能读取自身视角。
    std::optional<common::PlayerView> snapshot(PlayerInfo player) const;
    const storage::Replay& replay() const noexcept { return *replay_; }

private:
    int find_seat(const PlayerInfo& player) const;
    TableUpdate reject(const std::string& error) const;
    TableUpdate publish() const;
    common::PlayerView view_for(int seat) const;
    TableUpdate start_impl(TimePoint now,
        const std::array<common::Tile, Wall::tile_count>* tiles);
    // 将单次局内推进转换为牌桌状态、累计小分和按座位过滤的通知。
    TableUpdate consume(common::RoundTransition transition);
    // 连续执行自动摸牌或结算，直到需要等待玩家操作。
    TableUpdate advance();
    void refresh_prompts(const common::RoundTransition& transition);

    gymj::common::RuleConfig config_;
    common::TableOptions options_;
    // 保存近期请求的原始内容与回执，防止同一请求被重复执行。
    struct Receipt {
        common::Command command;
        bool accepted = false;
        std::string error;
        std::uint64_t table_seq = 0;
    };
    struct SeatState{
        bool is_seated = false;
        PlayerInfo player;
        bool is_ready = false;
        bool connected = false;
        std::uint64_t last_request_id = 0; // 即使回执已淘汰，也拒绝旧编号。
        std::deque<Receipt> receipts;
    };
    std::array<SeatState, 4> seats_;
    std::unique_ptr<gymj::room::Round> round_;
    std::unique_ptr<storage::Replay> replay_;
    std::mt19937 rng_;
    common::TableStage stage_ = common::TableStage::WaitingReady;
    std::uint64_t round_id_ = 0;
    std::uint64_t table_seq_ = 0;
    std::uint64_t prompt_id_ = 0; // 同一响应窗口内保持不变，不等同于状态序号。
    std::array<std::optional<TimePoint>, 4> deadlines_{};
    std::array<std::int64_t, 4> total_points_{};
    TimePoint now_{};
};

}



#endif
