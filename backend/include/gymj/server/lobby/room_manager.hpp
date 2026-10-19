#ifndef GYMJ_SERVER_ROOM_MANAGER_HPP
#define GYMJ_SERVER_ROOM_MANAGER_HPP

#include <gymj/core/room/table.hpp>
#include <gymj/server/session/session_manager.hpp>

#include <string_view>
#include <unordered_map>
#include <vector>

namespace gymj::server {

// 房间负责生命周期和投递索引；麻将动作是否合法仍完全由 Table 判定。
class Room {
public:
    Room(common::RuleConfig rules, common::TableOptions options);
    static constexpr std::string_view id = "room-001";

    common::TableUpdate join(const SessionManager::Session& session);
    common::TableUpdate leave(const SessionManager::Session& session);
    bool ready_to_start() const;
    bool abandoned() const;
    std::vector<SessionManager::Session> members() const;
    std::optional<common::TableUpdate> start_if_ready(bool retry = false);
    gateway::WsSession::Ptr connection_for(std::uint64_t player_id) const;
    std::vector<gateway::WsSession::Ptr> online_connections() const;
    room::Table& table() { return table_; }

private:
    room::Table table_;
    // 保存 Table 分配的内部 ID。昵称允许重复，不能拿昵称充当投递地址。
    std::unordered_map<std::uint64_t, SessionManager::Session> members_;
    std::optional<std::uint64_t> last_start_attempt_seq_;
};

class RoomManager {
public:
    explicit RoomManager(common::RuleConfig rules = {}, common::TableOptions options = {});
    Room* find(std::string_view id);
    Room* get_or_create(std::string_view id);
    void close(std::string_view id);

private:
    // v1 先验证单房间完整链路，未来扩房间时再替换为 roomId -> Room 映射。
    common::RuleConfig rules_;
    common::TableOptions options_;
    std::unique_ptr<Room> fixed_room_;
};
} // namespace gymj::server
#endif
