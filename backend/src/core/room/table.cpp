#include <gymj/core/room/table.hpp>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gymj::room {
using namespace gymj::common;

namespace {

std::uint64_t next_player_id() {
    // 全进程共享计数器，保证不同牌桌也不会分配相同 ID；它不是认证令牌。
    static std::atomic<std::uint64_t> next{1};
    auto id = next.load(std::memory_order_relaxed);
    for (;;) {
        if (id == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("player id space exhausted");
        }
        if (next.compare_exchange_weak(id, id + 1, std::memory_order_relaxed)) return id;
    }
}

void append_update(TableUpdate& out, TableUpdate next) {
    // 一次命令可能连带摸牌和结算，按发生顺序合并这些通知。
    out.table_seq = next.table_seq;
    out.replay_error = next.replay_error;
    out.deliveries.insert(out.deliveries.end(),
        std::make_move_iterator(next.deliveries.begin()),
        std::make_move_iterator(next.deliveries.end()));
    // 后续自动推进失败时保留原命令的接受状态，避免调用方重试已生效的动作。
    if (!next.accepted) out.error = next.error;
}

bool same_command(const Command& lhs, const Command& rhs) {
    // 编号相同但内容不同不属于重试，必须拒绝。
    return lhs.round_id == rhs.round_id && lhs.request_id == rhs.request_id
        && lhs.prompt_id == rhs.prompt_id && lhs.action.type == rhs.action.type
        && lhs.action.action_tile == rhs.action.action_tile;
}

std::vector<RoundEvent> visible_events(const std::vector<RoundEvent>& events, int seat) {
    // 过滤事件副本，隐藏对手摸牌、初始手牌、暗杠牌面及尚未裁决的响应选择。
    std::vector<RoundEvent> result;
    for (auto event : events) {
        if (event.player_seat != seat) {
            if (event.type == RoundEventType::ClaimSubmitted) continue;
            if (event.type == RoundEventType::InitialHands) event.tiles.clear();
            if (event.type == RoundEventType::PlayerDraw) event.tile.reset();
            if (event.type == RoundEventType::MeldDeclared && event.action
                && event.action->type == PlayerActionType::SelfKan) {
                event.tile.reset();
                event.action->action_tile = null_tile;
            }
        }
        result.push_back(std::move(event));
    }
    return result;
}

}

Table::Table(RuleConfig config, TableOptions options)
    : config_(config), options_(options) {
    if (config_.game.playerCount != 4) throw std::invalid_argument("table requires four players");
    if (options_.dealer_seat < 0 || options_.dealer_seat >= 4) {
        throw std::invalid_argument("invalid dealer seat");
    }
    if (options_.action_timeout_ms <= 0) throw std::invalid_argument("invalid action timeout");
    // 保留种子的高低 32 位，避免不同的 64 位种子被截断成相同随机序列。
    std::random_device random;
    const auto seed = options_.seed ? *options_.seed
        : (static_cast<std::uint64_t>(random()) << 32) | random();
    std::seed_seq seeds{static_cast<std::uint32_t>(seed), static_cast<std::uint32_t>(seed >> 32)};
    rng_.seed(seeds);
    replay_ = std::make_unique<storage::Replay>(options_.replay_directory);
}

int Table::find_seat(const PlayerInfo& player) const {
    // 未分配身份不能匹配空座位；昵称变化不影响身份识别。
    if (player.id == 0) return -1;
    for (int seat = 0; seat < 4; ++seat) {
        if (seats_[seat].is_seated && seats_[seat].player == player) return seat;
    }
    return -1;
}

TableUpdate Table::reject(const std::string& error) const {
    TableUpdate out;
    out.error = error;
    out.replay_error = replay_->error();
    out.table_seq = table_seq_;
    return out;
}

PlayerView Table::view_for(int seat) const {
    // 先构造公共桌面，再仅向本人提供手牌、合法动作和操作截止时间。
    PlayerView view;
    view.self_seat = seat;
    view.table_stage = stage_;
    view.table_seq = table_seq_;
    view.round_id = round_id_;
    view.last_request_id = seats_[seat].last_request_id;
    view.dealer_seat = options_.dealer_seat;
    view.total_points = total_points_;
    for (int i = 0; i < 4; ++i) {
        auto& visible = view.seats[i];
        visible.occupied = seats_[i].is_seated;
        visible.connected = seats_[i].connected;
        visible.ready = seats_[i].is_ready;
        visible.player_name = seats_[i].player.player_name;
    }
    if (!round_) return view;
    const auto& state = round_->state();
    view.round_stage = state.stage;
    view.round_seq = state.seq;
    view.acting_player = state.acting_player;
    view.tiles_remaining = state.tiles_remaining;
    view.own_tiles = state.states[seat];
    view.one_sou = state.one_sou;
    view.eight_pin = state.eight_pin;
    view.pending_action = state.pending_action;
    view.available_actions = round_->available_actions()[seat];
    view.deadline = deadlines_[seat];
    if (view.deadline) view.prompt_id = prompt_id_;
    for (int i = 0; i < 4; ++i) {
        auto& visible = view.seats[i];
        visible.hand_size = static_cast<int>(state.states[i].hand.size());
        visible.river = state.states[i].river;
        visible.melds = state.states[i].melds;
        if (i != seat && state.stage != RoundStage::Ended) {
            for (auto& meld : visible.melds) {
                if (meld.type == MeldType::SelfKan) meld.tile = null_tile;
            }
        }
    }
    // 当前约定在小局结束后公开完整结局，等待响应时仍隐藏其他人的手牌。
    if (state.stage == RoundStage::Ended) {
        view.round_result = round_->result();
        view.point_result = round_->point_result();
    }
    return view;
}

TableUpdate Table::publish() const {
    // 只为在线座位生成通知；断线玩家通过重连快照恢复状态。
    TableUpdate out;
    out.accepted = true;
    out.replay_error = replay_->error();
    out.table_seq = table_seq_;
    for (int seat = 0; seat < 4; ++seat) {
        if (seats_[seat].is_seated && seats_[seat].connected) {
            out.deliveries.push_back(Delivery{seats_[seat].player.id, view_for(seat), {}});
        }
    }
    return out;
}

TableUpdate Table::join(PlayerInfo player) {
    const int existing = find_seat(player);
    if (existing >= 0) {
        // 重连复用原有身份和座位，不重置局内操作窗口。
        if (!seats_[existing].connected) {
            seats_[existing].connected = true;
            ++table_seq_;
        }
        auto out = publish();
        out.assigned_player = seats_[existing].player;
        return out;
    }
    if (player.id != 0) return reject("unknown player id");
    if (stage_ == TableStage::Playing) return reject("round in progress");
    for (auto& seat : seats_) {
        if (!seat.is_seated) {
            player.id = next_player_id();
            seat.is_seated = true;
            seat.player = std::move(player);
            seat.connected = true;
            ++table_seq_;
            auto out = publish();
            out.assigned_player = seat.player;
            return out;
        }
    }
    return reject("table is full");
}

TableUpdate Table::ready(PlayerInfo player) {
    const int seat = find_seat(player);
    if (seat < 0) return reject("player not seated");
    if (!seats_[seat].connected) return reject("player disconnected");
    if (stage_ == TableStage::Playing) return reject("round in progress");
    if (!seats_[seat].is_ready) {
        seats_[seat].is_ready = true;
        ++table_seq_;
    }
    return publish();
}

TableUpdate Table::start() { return start(Clock::now()); }
TableUpdate Table::start(TimePoint now) { return start_impl(now, nullptr); }
TableUpdate Table::start(const std::array<Tile, Wall::tile_count>& tiles, TimePoint now) {
    return start_impl(now, &tiles);
}

TableUpdate Table::start_impl(TimePoint now, const std::array<Tile, Wall::tile_count>* tiles) {
    // 全员在线且准备后才允许开局，进行中的小局不能被替换。
    if (stage_ == TableStage::Playing) return reject("round in progress");
    std::array<PlayerInfo, 4> players;
    for (int seat = 0; seat < 4; ++seat) {
        if (!seats_[seat].is_seated || !seats_[seat].connected || !seats_[seat].is_ready) {
            return reject("four connected ready players required");
        }
        players[seat] = seats_[seat].player;
    }
    RoundConfig config;
    config.rule = config_;
    config.dealer_seat = options_.dealer_seat;
    config.action_timeout_ms = options_.action_timeout_ms;
    // 使用临时对象尝试开局，失败时不消耗正式随机序列，也不覆盖上一局。
    auto next_rng = rng_;
    config.seed = static_cast<std::uint64_t>(next_rng()) << 32;
    config.seed |= next_rng();
    auto next_round = std::make_unique<Round>(config, players);
    auto transition = tiles ? next_round->start(*tiles) : next_round->start();
    if (!transition.accepted) return reject(transition.error);

    if (!replay_->start_round(round_id_ + 1, config, players, tiles != nullptr)) {
        return reject("cannot start round: replay unavailable");
    }

    rng_ = next_rng;
    round_ = std::move(next_round);
    ++round_id_;
    stage_ = TableStage::Playing;
    now_ = std::max(now_, now);
    for (auto& seat : seats_) {
        seat.is_ready = false;
        seat.last_request_id = 0;
        seat.receipts.clear();
    }
    auto out = consume(std::move(transition));
    append_update(out, advance());
    return out;
}

TableUpdate Table::disconnect(PlayerInfo player) {
    // 断线仅更新连接和准备状态，局内座位保留，超时仍由 tick 处理。
    const int seat = find_seat(player);
    if (seat < 0) return reject("player not seated");
    if (seats_[seat].connected) {
        seats_[seat].connected = false;
        seats_[seat].is_ready = false;
        ++table_seq_;
    }
    return publish();
}

TableUpdate Table::submit(PlayerInfo player, Command command) {
    return submit(player, command, Clock::now());
}

TableUpdate Table::submit(PlayerInfo player, Command command, TimePoint now) {
    auto fail = [&](const std::string& error) {
        auto out = reject(error);
        out.request_id = command.request_id;
        return out;
    };
    const int seat = find_seat(player);
    if (seat < 0) return fail("player not seated");
    auto& owner = seats_[seat];
    if (!owner.connected) return fail("player disconnected");
    if (!round_ || command.round_id != round_id_) return fail("invalid round id");
    if (command.request_id == 0) return fail("request id must be positive");
    // 先处理重复请求，即使原动作已经使窗口或小局结束，也返回原回执。
    for (const auto& receipt : owner.receipts) {
        if (receipt.command.request_id != command.request_id) continue;
        if (!same_command(receipt.command, command)) return fail("request id reused with different command");
        TableUpdate out;
        out.accepted = receipt.accepted;
        out.error = receipt.error;
        out.table_seq = receipt.table_seq;
        out.request_id = command.request_id;
        out.duplicate = true;
        out.replay_error = replay_->error();
        return out;
    }
    if (command.request_id <= owner.last_request_id) return fail("request id is too old");
    // 时间只向前推进；通过基础校验的新请求会占用编号，包括非法动作请求。
    now_ = std::max(now_, now);
    owner.last_request_id = command.request_id;
    TableUpdate out;
    if (stage_ != TableStage::Playing) out = fail("round is not playing");
    else if (command.prompt_id != prompt_id_ || !deadlines_[seat]) out = fail("invalid action prompt");
    else if (now_ >= *deadlines_[seat]) out = fail("action deadline expired");
    else {
        out = consume(round_->submit_action(seat, command.action));
        if (out.accepted) append_update(out, advance());
    }
    out.request_id = command.request_id;
    // 限制回执缓存大小；更早的请求仍由 last_request_id 阻止再次执行。
    owner.receipts.push_back(Receipt{command, out.accepted, out.error, out.table_seq});
    if (owner.receipts.size() > 128) owner.receipts.pop_front();
    return out;
}

void Table::refresh_prompts(const RoundTransition& transition) {
    // 多人响应期间只移除已回复者的截止时间，不延长其他人的倒计时。
    const bool same_claim = transition.stage_before == RoundStage::WaitingClaim
        && transition.stage_after == RoundStage::WaitingClaim;
    if (!same_claim) {
        deadlines_ = {};
        if (transition.stage_after == RoundStage::WaitingDiscard
            || transition.stage_after == RoundStage::WaitingClaim) ++prompt_id_;
    }
    for (int seat = 0; seat < 4; ++seat) {
        if (transition.available_actions[seat].empty()) deadlines_[seat].reset();
        else if (!deadlines_[seat]) {
            deadlines_[seat] = now_ + std::chrono::milliseconds(options_.action_timeout_ms);
        }
    }
}

TableUpdate Table::consume(RoundTransition transition) {
    if (!transition.accepted) return reject(transition.error);
    ++table_seq_;
    refresh_prompts(transition);
    // 仅在本次真正产生计分事件时累加，避免重复读取结局导致重复计分。
    const bool settled = std::any_of(transition.events.begin(), transition.events.end(),
        [](const RoundEvent& event) { return event.type == RoundEventType::PointsCalculated; });
    if (settled) {
        for (int seat = 0; seat < 4; ++seat) total_points_[seat] += transition.point_result->delta_result[seat];
        stage_ = TableStage::RoundFinished;
    }
    // Persist full events once, before per-player filtering removes hidden tiles.
    replay_->record(round_id_, table_seq_, transition, total_points_);
    auto out = publish();
    for (auto& delivery : out.deliveries) {
        delivery.events = visible_events(transition.events, delivery.view.self_seat);
    }
    return out;
}

TableUpdate Table::advance() {
    // 自动阶段无需客户端请求；遇到出牌或响应阶段便停止。
    TableUpdate out;
    out.accepted = true;
    out.table_seq = table_seq_;
    out.replay_error = replay_->error();
    while (round_ && stage_ == TableStage::Playing) {
        RoundTransition transition;
        if (round_->state().stage == RoundStage::WaitingDraw) transition = round_->draw_for_current_player();
        else if (round_->state().stage == RoundStage::Ended && !round_->point_result()) transition = round_->settle();
        else break;
        auto next = consume(std::move(transition));
        const bool accepted = next.accepted;
        append_update(out, std::move(next));
        if (!accepted) {
            out.accepted = false;
            break;
        }
    }
    return out;
}

TableUpdate Table::tick() { return tick(Clock::now()); }
TableUpdate Table::tick(TimePoint now) {
    now_ = std::max(now_, now);
    TableUpdate out;
    out.accepted = true;
    out.table_seq = table_seq_;
    out.replay_error = replay_->error();
    // 固定进入 tick 时的窗口；旧窗口结束后，不能继续超时处理新窗口的玩家。
    const auto prompt = prompt_id_;
    const auto expired = deadlines_;
    for (int seat = 0; seat < 4; ++seat) {
        if (stage_ != TableStage::Playing || prompt_id_ != prompt) break;
        if (!expired[seat] || now_ < *expired[seat] || !deadlines_[seat]) continue;
        auto next = consume(round_->handle_timeout(seat));
        const bool accepted = next.accepted;
        append_update(out, std::move(next));
        if (!accepted) {
            out.accepted = false;
            break;
        }
        append_update(out, advance());
        if (!out.error.empty()) break;
    }
    return out;
}

std::optional<PlayerView> Table::snapshot(PlayerInfo player) const {
    const int seat = find_seat(player);
    if (seat < 0) return std::nullopt;
    return view_for(seat);
}

}
