#ifndef GYMJ_SERVER_GATEWAY_PROTOCOL_HPP
#define GYMJ_SERVER_GATEWAY_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <gymj/common/schema/table_state.hpp>
#include <nlohmann/json_fwd.hpp>

namespace gymj::server::gateway::protocol {

using Json = nlohmann::json;

inline constexpr int version = 1;
inline constexpr std::size_t max_message_bytes = 64 * 1024;
inline constexpr int max_json_depth = 32;

// Stable wire error codes. Never send internal exception text as a code.
enum class ErrorCode {
    InvalidJson,
    JsonTooDeep,
    TextOnly,
    UnsupportedVersion,
    InvalidType,
    UnknownType,
    InvalidField,
    InvalidTile,
    InvalidAction,
    HelloRequired,
    AlreadyAuthenticated,
    InvalidNickname,
    InvalidSession,
    SessionExpired,
    NotSeated,
    RoomNotFound,
    RoomMismatch,
    RoomFull,
    AlreadyInRoom,
    StaleRound,
    StalePrompt,
    StaleRequest,
    ActionNotAllowed,
    RateLimited,
    InternalError
};

std::string_view to_string(ErrorCode code) noexcept;

struct ProtocolError {
    ErrorCode code = ErrorCode::InternalError;
    std::string message;
    std::optional<std::string> request_tag;
    std::optional<std::uint64_t> request_id;
};

struct HelloRequest {
    std::optional<std::string> nickname;
    std::optional<std::string> session_token;
};

struct PingRequest {
    std::optional<std::string> nonce;
};

struct JoinRoomRequest {
    std::string request_tag;
    std::string room_id;
};

struct ReadyRequest {
    std::string request_tag;
    std::string room_id;
};

struct SnapshotRequest {
    std::string request_tag;
    std::string room_id;
};

struct ActionRequest {
    std::string room_id;
    common::Command command;
};

using ClientRequest = std::variant<
    HelloRequest,
    PingRequest,
    JoinRoomRequest,
    ReadyRequest,
    SnapshotRequest,
    ActionRequest>;

struct DecodeResult {
    std::optional<ClientRequest> request;
    std::optional<ProtocolError> error;

    explicit operator bool() const noexcept { return request.has_value(); }
};

// Parses JSON and validates the complete v1 message shape. Authentication and
// room authorization belong to Router, not this decoder.
DecodeResult decode_client_message(std::string_view text);

// Exposed for focused protocol tests. Only canonical decimal strings pass.
std::optional<std::uint64_t> parse_u64(const Json& value) noexcept;

struct Welcome {
    std::string session_token;
    std::string nickname;
    bool resumed = false;
    std::optional<std::string> room_id;
};

enum class LobbyOperation {
    JoinRoom,
    Ready,
    Snapshot
};

struct LobbyAck {
    LobbyOperation operation = LobbyOperation::JoinRoom;
    std::string request_tag;
    bool accepted = false;
    std::optional<ErrorCode> error;
};

struct ActionAck {
    std::uint64_t request_id = 0;
    bool accepted = false;
    bool duplicate = false;
    std::uint64_t table_seq = 0;
    std::optional<ErrorCode> error;
};

struct ServerNotice {
    std::string code;
    std::string message;
};

Json encode_welcome(const Welcome& value);
Json encode_pong(const PingRequest& request);
Json encode_ack(const LobbyAck& value);
Json encode_ack(const ActionAck& value);
Json encode_error(const ProtocolError& value);
Json encode_notice(const ServerNotice& value);

// Delivery already contains the recipient-filtered PlayerView and events.
// The encoder must not accept RoundState or unfiltered authoritative data.
Json encode_table_update(
    std::string_view room_id,
    const common::Delivery& delivery,
    common::TableClock::time_point now);

} // namespace gymj::server::gateway::protocol

#endif
