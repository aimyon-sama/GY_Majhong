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

bool Room::ready_to_start() const {
    if (members_.size() != 4) return false;
    const auto view = table_.snapshot(*members_.begin()->second->player);
    return view && view->table_stage != common::TableStage::Playing
        && std::all_of(view->seats.begin(), view->seats.end(), [](const auto& seat) {
            return seat.occupied && seat.connected && seat.ready;
        });
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
    : fixed_room_(rules, std::move(options)) {}

Room* RoomManager::find(std::string_view id) {
    return id == Room::id ? &fixed_room_ : nullptr;
}
} // namespace gymj::server
