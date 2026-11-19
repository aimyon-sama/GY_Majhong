#ifndef GYMJ_SERVER_ROOM_MANAGER_HPP
#define GYMJ_SERVER_ROOM_MANAGER_HPP

#include <gymj/core/room/table.hpp>
#include <gymj/server/session/session_manager.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

namespace gymj::server {

struct RoomSummary {
    std::string id;
    std::size_t players = 0;
    std::size_t connected = 0;
    bool playing = false;
};

// 房间负责生命周期和投递索引；麻将动作是否合法仍完全由 Table 判定。
class Room {
public:
    Room(std::string id, common::RuleConfig rules, common::TableOptions options);

    const std::string& id() const noexcept { return id_; }
    RoomSummary summary() const;

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
    std::string id_;
    room::Table table_;
    // 保存 Table 分配的内部 ID。昵称允许重复，不能拿昵称充当投递地址。
    std::unordered_map<std::uint64_t, SessionManager::Session> members_;
    std::optional<std::uint64_t> last_start_attempt_seq_;
};

class RoomManager {
public:
    static constexpr std::string_view default_room_id = "100001";
    static constexpr std::size_t max_rooms = 50;

    explicit RoomManager(common::RuleConfig rules = {}, common::TableOptions options = {});
    Room* find(std::string_view id);
    Room* create(std::string id);
    void close(std::string_view id);
    std::vector<Room*> active_rooms();
    std::vector<RoomSummary> summaries() const;
    std::size_t size() const noexcept { return rooms_.size(); }
    static bool valid_id(std::string_view id) noexcept;

private:
    common::TableOptions options_for(std::string_view id) const;
    common::RuleConfig rules_;
    common::TableOptions options_;
    std::unordered_map<std::string, std::unique_ptr<Room>> rooms_;
};
} // namespace gymj::server
#endif
