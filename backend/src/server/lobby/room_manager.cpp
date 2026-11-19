#include <gymj/server/lobby/room_manager.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace gymj::server {
Room::Room(std::string id, common::RuleConfig rules, common::TableOptions options)
    : id_(std::move(id)), table_(rules, std::move(options)) {}

RoomSummary Room::summary() const {
    RoomSummary result;
    result.id = id_;
    result.players = members_.size();
    if (members_.empty()) return result;
    const auto view = table_.snapshot(*members_.begin()->second->player);
    if (!view) return result;
    result.playing = view->table_stage == common::TableStage::Playing;
    result.connected = static_cast<std::size_t>(std::count_if(
        view->seats.begin(), view->seats.end(), [](const auto& seat) {
            return seat.occupied && seat.connected;
        }));
    return result;
}

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
        session->room_id = id_;
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
    : rules_(rules), options_(std::move(options)) {
    create(std::string(default_room_id));
}

Room* RoomManager::find(std::string_view id) {
    const auto found = rooms_.find(std::string(id));
    return found == rooms_.end() ? nullptr : found->second.get();
}

Room* RoomManager::create(std::string id) {
    if (!valid_id(id) || rooms_.size() >= max_rooms || rooms_.contains(id)) return nullptr;
    auto room = std::make_unique<Room>(id, rules_, options_for(id));
    auto* result = room.get();
    rooms_.emplace(std::move(id), std::move(room));
    return result;
}

void RoomManager::close(std::string_view id) {
    rooms_.erase(std::string(id));
}

std::vector<Room*> RoomManager::active_rooms() {
    std::vector<Room*> result;
    result.reserve(rooms_.size());
    for (auto& [id, room] : rooms_) result.push_back(room.get());
    return result;
}

std::vector<RoomSummary> RoomManager::summaries() const {
    std::vector<RoomSummary> result;
    result.reserve(rooms_.size());
    for (const auto& [id, room] : rooms_) result.push_back(room->summary());
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });
    return result;
}

bool RoomManager::valid_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 32) return false;
    return std::all_of(id.begin(), id.end(), [](char ch) {
        return ch >= '0' && ch <= '9';
    });
}

common::TableOptions RoomManager::options_for(std::string_view id) const {
    auto result = options_;
    if (result.seed && id != default_room_id) {
        // 固定种子模式下仍让不同房间拥有不同且可复现的洗牌序列。
        std::uint64_t hash = 1469598103934665603ULL;
        for (const unsigned char ch : id) {
            hash ^= ch;
            hash *= 1099511628211ULL;
        }
        result.seed = *result.seed ^ hash;
    }
    return result;
}
} // namespace gymj::server
