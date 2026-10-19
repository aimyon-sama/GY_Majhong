#include <gymj/server/lobby/room_manager.hpp>

#include <algorithm>
#include <utility>

namespace gymj::server {
Room::Room(common::RuleConfig rules, common::TableOptions options)
    : table_(rules, std::move(options)) {}

common::TableUpdate Room::join(const SessionManager::Session& session) {
    // 进行中的 Table 优先报告 round in progress；房间层仍应明确报告满员。
    if (!session->player && members_.size() == 4) {
        common::TableUpdate rejected;
        rejected.error = "table is full";
        return rejected;
    }
    auto update = table_.join(session->player.value_or(common::PlayerInfo{session->nickname, 0}));
    if (update.accepted && update.assigned_player) {
        session->player = update.assigned_player;
        session->room_id = std::string(id);
        // 必须先完成绑定，Router 随后才能把首次私有快照发给刚入座的人。
        members_[session->player->id] = session;
    }
    return update;
}

common::TableUpdate Room::leave(const SessionManager::Session& session) {
    // 未入座时重复离开也成功；已入座身份始终绑定原座位，禁止补位。
    if (!session->player) {
        common::TableUpdate update;
        update.accepted = true;
        return update;
    }
    return table_.leave(*session->player);
}

bool Room::abandoned() const {
    if (members_.empty()) return false;
    const auto view = table_.snapshot(*members_.begin()->second->player);
    return view && std::none_of(view->seats.begin(), view->seats.end(), [](const auto& seat) {
        return seat.occupied && seat.connected;
    });
}

std::vector<SessionManager::Session> Room::members() const {
    std::vector<SessionManager::Session> result;
    for (const auto& [id, session] : members_) result.push_back(session);
    return result;
}

bool Room::ready_to_start() const {
    if (members_.size() != 4 || abandoned()) return false;
    const auto view = table_.snapshot(*members_.begin()->second->player);
    return view && view->table_stage != common::TableStage::Playing
        && std::all_of(view->seats.begin(), view->seats.end(), [](const auto& seat) {
            return seat.occupied && seat.ready;
        });
}

std::optional<common::TableUpdate> Room::start_if_ready(bool retry) {
    if (!ready_to_start()) return std::nullopt;
    const auto seq = table_.snapshot(*members_.begin()->second->player)->table_seq;
    // 定时器可持续开局，但存储故障时不能每 25ms 重试并刷屏。
    // 状态变化或玩家再次准备后允许重试。
    if (!retry && last_start_attempt_seq_ == seq) return std::nullopt;
    last_start_attempt_seq_ = seq;
    return table_.start();
}

gateway::WsSession::Ptr Room::connection_for(std::uint64_t player_id) const {
    const auto member = members_.find(player_id);
    return member == members_.end() ? nullptr : member->second->connection.lock();
}

std::vector<gateway::WsSession::Ptr> Room::online_connections() const {
    std::vector<gateway::WsSession::Ptr> result;
    for (const auto& [id, member] : members_) {
        if (auto connection = member->connection.lock()) result.push_back(std::move(connection));
    }
    return result;
}

RoomManager::RoomManager(common::RuleConfig rules, common::TableOptions options)
    : rules_(rules), options_(std::move(options)),
      fixed_room_(std::make_unique<Room>(rules_, options_)) {}

Room* RoomManager::find(std::string_view id) {
    return id == Room::id ? fixed_room_.get() : nullptr;
}
Room* RoomManager::get_or_create(std::string_view id) {
    if (id != Room::id) return nullptr;
    if (!fixed_room_) fixed_room_ = std::make_unique<Room>(rules_, options_);
    return fixed_room_.get();
}

void RoomManager::close(std::string_view id) {
    if (id == Room::id) fixed_room_.reset();
}
} // namespace gymj::server
