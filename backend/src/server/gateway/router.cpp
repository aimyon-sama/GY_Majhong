#include <gymj/server/gateway/router.hpp>

#include <nlohmann/json.hpp>

#include <type_traits>
#include <utility>

namespace gymj::server::gateway {
namespace {
using protocol::ErrorCode;

void send(const WsSession::Ptr& connection, const protocol::Json& message) {
    connection->send_text(message.dump());
}

void error(const WsSession::Ptr& connection, ErrorCode code,
           std::optional<std::string> tag = {}, std::optional<std::uint64_t> request_id = {}) {
    // 只发送稳定错误码，不把内部异常、文件路径或认证令牌拼进错误信息。
    send(connection, protocol::encode_error({code, std::string(protocol::to_string(code)),
                                             std::move(tag), request_id}));
}

ErrorCode table_error(std::string_view error) {
    // 在适配层集中转换核心错误，避免客户端依赖 Table 的英文诊断文本。
    if (error == "table is full") return ErrorCode::RoomFull;
    if (error == "player not seated") return ErrorCode::NotSeated;
    if (error == "invalid round id") return ErrorCode::StaleRound;
    if (error == "invalid action prompt" || error == "action deadline expired") return ErrorCode::StalePrompt;
    if (error == "request id must be positive" || error == "request id is too old"
        || error == "request id reused with different command") return ErrorCode::StaleRequest;
    if (error == "illegal action" || error == "round in progress"
        || error == "round is not playing" || error == "player disconnected") return ErrorCode::ActionNotAllowed;
    return ErrorCode::InternalError;
}

void acknowledge(const WsSession::Ptr& connection, protocol::LobbyOperation operation,
                 const std::string& tag, const common::TableUpdate& update) {
    send(connection, protocol::encode_ack({operation, tag, update.accepted,
        update.accepted ? std::nullopt : std::optional{table_error(update.error)}}));
}
} // namespace

Router::Router(boost::asio::io_context& ioc, common::RuleConfig rules, common::TableOptions options)
    : rooms_(rules, std::move(options)), timer_(ioc) {}

void Router::start() {
    if (running_) return;
    running_ = true;
    schedule_tick();
}

void Router::stop() {
    running_ = false;
    timer_.cancel();
}

void Router::schedule_tick() {
    // 单调时钟 + 单线程事件循环：计时器和客户端动作不会并发修改 Table。
    // 固定间隔避免忙轮询；一次 tick 只处理当前窗口，不追赶执行新窗口。
    timer_.expires_after(std::chrono::milliseconds(25));
    timer_.async_wait([weak = weak_from_this()](boost::system::error_code ec) {
        const auto self = weak.lock();
        if (ec || !self || !self->running_) return;
        if (auto* room = self->rooms_.find(Room::id)) {
            const auto update = room->table().tick();
            // 空闲 tick 不发快照，也不每 25ms 重复广播旧的存储告警。
            if (!update.deliveries.empty() || !update.error.empty()) self->deliver(*room, update);
        }
        self->schedule_tick();
    });
}

void Router::on_message(WsSession::Ptr connection, std::string text) {
    // 被接管的旧连接即使还有已排队消息，也不能重新认证或继续操纵原座位。
    if (sessions_.superseded(connection)) return connection->close();
    auto decoded = protocol::decode_client_message(text);
    if (!decoded) return send(connection, protocol::encode_error(*decoded.error));
    std::visit([&](const auto& request) {
        using Request = std::decay_t<decltype(request)>;
        if constexpr (std::is_same_v<Request, protocol::PingRequest>) {
            send(connection, protocol::encode_pong(request));
        } else if constexpr (std::is_same_v<Request, protocol::HelloRequest>) {
            hello(connection, request);
        } else {
            auto session = sessions_.current(connection);
            if (!session) {
                if constexpr (std::is_same_v<Request, protocol::ActionRequest>) {
                    error(connection, ErrorCode::HelloRequired, {}, request.command.request_id);
                } else {
                    error(connection, ErrorCode::HelloRequired, request.request_tag);
                }
                return;
            }
            handle(connection, session, request);
        }
    }, *decoded.request);
}

void Router::hello(const WsSession::Ptr& connection, const protocol::HelloRequest& request) {
    auto auth = sessions_.authenticate(connection, request);
    if (auth.error) return error(connection, *auth.error);
    if (auth.previous_connection) auth.previous_connection->close();
    const auto& session = auth.session;
    send(connection, protocol::encode_welcome({session->token, session->nickname,
                                               auth.resumed, session->room_id}));
    // 身份已绑定才恢复原座位。Table::join 复用内部 ID，不重置操作期限或编号。
    if (session->room_id && sessions_.current(connection)) {
        auto* room = rooms_.find(*session->room_id);
        if (!room) return error(connection, ErrorCode::InternalError);
        const auto update = room->join(session);
        deliver(*room, update);
    }
}

void Router::handle(const WsSession::Ptr& connection, const Session& session,
                    const protocol::JoinRoomRequest& request) {
    auto* room = rooms_.find(request.room_id);
    if (!room) return error(connection, ErrorCode::RoomNotFound, request.request_tag);
    if (session->room_id && *session->room_id != request.room_id) {
        return error(connection, ErrorCode::AlreadyInRoom, request.request_tag);
    }
    const auto update = room->join(session);
    acknowledge(connection, protocol::LobbyOperation::JoinRoom, request.request_tag, update);
    deliver(*room, update);
}

Room* Router::seated_room(const WsSession::Ptr& connection, const Session& session,
                         std::string_view room_id, std::optional<std::string> tag,
                         std::optional<std::uint64_t> request_id) {
    if (!session->room_id || !session->player) {
        error(connection, ErrorCode::NotSeated, std::move(tag), request_id);
        return nullptr;
    }
    if (*session->room_id != room_id) {
        error(connection, ErrorCode::RoomMismatch, std::move(tag), request_id);
        return nullptr;
    }
    auto* room = rooms_.find(room_id);
    if (!room) error(connection, ErrorCode::RoomNotFound, std::move(tag), request_id);
    return room;
}

void Router::handle(const WsSession::Ptr& connection, const Session& session,
                    const protocol::ReadyRequest& request) {
    auto* room = seated_room(connection, session, request.room_id, request.request_tag);
    if (!room) return;
    const auto update = room->table().ready(*session->player);
    acknowledge(connection, protocol::LobbyOperation::Ready, request.request_tag, update);
    deliver(*room, update);
    if (update.accepted && room->ready_to_start()) {
        const auto started = room->table().start();
        // 准备成功和自动开局是两件事；开局写牌谱失败不能把准备回执改成失败。
        if (!started.accepted) notice(*room, {"start_failed", "Unable to start round"});
        deliver(*room, started);
    }
}

void Router::handle(const WsSession::Ptr& connection, const Session& session,
                    const protocol::SnapshotRequest& request) {
    auto* room = seated_room(connection, session, request.room_id, request.request_tag);
    if (!room) return;
    const auto view = room->table().snapshot(*session->player);
    if (!view) return error(connection, ErrorCode::NotSeated, request.request_tag);
    send(connection, protocol::encode_ack(protocol::LobbyAck{
        protocol::LobbyOperation::Snapshot, request.request_tag, true, {}}));
    send(connection, protocol::encode_table_update(Room::id,
        common::Delivery{session->player->id, *view, {}}, common::TableClock::now()));
}

void Router::handle(const WsSession::Ptr& connection, const Session& session,
                    const protocol::ActionRequest& request) {
    auto* room = seated_room(connection, session, request.room_id, {}, request.command.request_id);
    if (!room) return;
    // 身份只来自服务端会话。请求编号、重复动作和截止时间交给已有 Table 统一裁决。
    const auto update = room->table().submit(*session->player, request.command);
    send(connection, protocol::encode_ack(protocol::ActionAck{request.command.request_id,
        update.accepted, update.duplicate, update.table_seq,
        update.accepted ? std::nullopt : std::optional{table_error(update.error)}}));
    deliver(*room, update);
}

void Router::deliver(Room& room, const common::TableUpdate& update) {
    // 一次出牌可能带来响应、摸牌和结算，必须保留每份 Delivery 的顺序及序号。
    // 这里只编码 Table 已脱敏的视角，绝不能把某一人的私有快照广播给全桌。
    const auto now = common::TableClock::now();
    for (const auto& delivery : update.deliveries) {
        if (auto connection = room.connection_for(delivery.recipient_id)) {
            send(connection, protocol::encode_table_update(Room::id, delivery, now));
        }
    }
    if (!update.replay_error.empty()) notice(room, {"replay_write_failed", "Replay storage unavailable"});
    if (update.accepted && !update.error.empty()) {
        notice(room, {"advance_failed", "Unable to advance round"});
    }
}

void Router::notice(Room& room, const protocol::ServerNotice& value) {
    for (const auto& connection : room.online_connections()) send(connection, protocol::encode_notice(value));
}

void Router::on_close(WsSession::Ptr connection) {
    const auto session = sessions_.disconnect(connection);
    if (!session || !session->room_id || !session->player) return;
    if (auto* room = rooms_.find(*session->room_id)) {
        deliver(*room, room->table().disconnect(*session->player));
    }
}
} // namespace gymj::server::gateway
