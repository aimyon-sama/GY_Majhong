#include <gymj/server/gateway/protocol.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <initializer_list>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace gymj::server::gateway::protocol {
namespace {

ProtocolError make_error(ErrorCode code, std::string message) {
    ProtocolError result;
    result.code = code;
    result.message = std::move(message);
    return result;
}

DecodeResult fail(ErrorCode code, std::string message) {
    DecodeResult result;
    result.error = make_error(code, std::move(message));
    return result;
}

DecodeResult succeed(ClientRequest request) {
    DecodeResult result;
    result.request = std::move(request);
    return result;
}

bool check_json_depth(std::string_view text) {
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char ch : text) {
        if (in_string) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') in_string = false;
            continue;
        }
        if (ch == '"') in_string = true;
        else if (ch == '{' || ch == '[') {
            if (++depth > max_json_depth) return false;
        } else if (ch == '}' || ch == ']') {
            --depth;
        }
    }
    return true;
}

bool only_contains(const Json& object, std::initializer_list<std::string_view> fields) {
    for (const auto& [key, value] : object.items()) {
        (void)value;
        if (std::find(fields.begin(), fields.end(), key) == fields.end()) return false;
    }
    return true;
}

const Json* find_field(const Json& object, std::string_view key) {
    const auto it = object.find(std::string(key));
    return it == object.end() ? nullptr : &*it;
}

std::optional<std::string> required_string(
    const Json& object,
    std::string_view key,
    std::size_t min_bytes,
    std::size_t max_bytes)
{
    const auto* value = find_field(object, key);
    if (!value || !value->is_string()) return std::nullopt;
    const auto& text = value->get_ref<const std::string&>();
    if (text.size() < min_bytes || text.size() > max_bytes) return std::nullopt;
    return text;
}

std::optional<std::string> optional_string(
    const Json& object,
    std::string_view key,
    std::size_t max_bytes,
    bool& valid)
{
    valid = true;
    const auto* value = find_field(object, key);
    if (!value) return std::nullopt;
    if (!value->is_string()) {
        valid = false;
        return std::nullopt;
    }
    const auto& text = value->get_ref<const std::string&>();
    if (text.size() > max_bytes) {
        valid = false;
        return std::nullopt;
    }
    return text;
}

std::string trim_ascii_whitespace(std::string text) {
    const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    const auto first = std::find_if(text.begin(), text.end(), not_space);
    const auto last = std::find_if(text.rbegin(), text.rend(), not_space).base();
    return first < last ? std::string(first, last) : std::string{};
}

bool valid_nickname(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        std::uint32_t codepoint = 0;
        std::size_t length = 0;
        if (first < 0x80) {
            codepoint = first;
            length = 1;
        } else if ((first & 0xe0) == 0xc0) {
            codepoint = first & 0x1f;
            length = 2;
        } else if ((first & 0xf0) == 0xe0) {
            codepoint = first & 0x0f;
            length = 3;
        } else if ((first & 0xf8) == 0xf0) {
            codepoint = first & 0x07;
            length = 4;
        } else return false;

        if (i + length > text.size()) return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto byte = static_cast<unsigned char>(text[i + offset]);
            if ((byte & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if ((length == 2 && codepoint < 0x80)
            || (length == 3 && codepoint < 0x800)
            || (length == 4 && codepoint < 0x10000)
            || (codepoint >= 0xd800 && codepoint <= 0xdfff)
            || codepoint > 0x10ffff
            || codepoint <= 0x1f
            || (codepoint >= 0x7f && codepoint <= 0x9f)) return false;
        if (++count > 24) return false;
        i += length;
    }
    return count != 0;
}

bool valid_session_token(std::string_view token) {
    return token.size() == 64 && std::all_of(token.begin(), token.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

std::optional<common::Tile> parse_tile(const Json& value) {
    if (!value.is_string()) return std::nullopt;
    const auto& text = value.get_ref<const std::string&>();
    if (text.size() != 2 || text[0] < '1' || text[0] > '9') return std::nullopt;
    common::TileType type = common::TileType::None;
    switch (text[1]) {
        case 'm': type = common::TileType::Man; break;
        case 's': type = common::TileType::Sou; break;
        case 'p': type = common::TileType::Pin; break;
        default: return std::nullopt;
    }
    return common::Tile{type, static_cast<std::uint8_t>(text[0] - '0')};
}

struct ParsedAction {
    std::optional<common::PlayerAction> value;
    ErrorCode error = ErrorCode::InvalidAction;
};

ParsedAction parse_action(const Json& object) {
    if (!object.is_object()) return {};
    const auto type = required_string(object, "type", 1, 32);
    if (!type) return {};

    common::PlayerActionType action_type = common::PlayerActionType::None;
    bool needs_tile = false;
    if (*type == "discard") { action_type = common::PlayerActionType::Discard; needs_tile = true; }
    else if (*type == "pon") { action_type = common::PlayerActionType::Pon; needs_tile = true; }
    else if (*type == "self_kan") { action_type = common::PlayerActionType::SelfKan; needs_tile = true; }
    else if (*type == "add_kan") { action_type = common::PlayerActionType::AddKan; needs_tile = true; }
    else if (*type == "open_kan") { action_type = common::PlayerActionType::OpenKan; needs_tile = true; }
    else if (*type == "tsumo") action_type = common::PlayerActionType::Tsumo;
    else if (*type == "ron") action_type = common::PlayerActionType::Ron;
    else if (*type == "pass") action_type = common::PlayerActionType::Pass;
    else return {};

    if (!needs_tile) {
        if (!only_contains(object, {"type"})) return {};
        ParsedAction result;
        result.value = common::PlayerAction{action_type, common::null_tile};
        return result;
    }

    if (!only_contains(object, {"type", "tile"})) return {};
    const auto* tile_json = find_field(object, "tile");
    if (!tile_json) return {};
    const auto tile = parse_tile(*tile_json);
    if (!tile) {
        ParsedAction result;
        result.error = ErrorCode::InvalidTile;
        return result;
    }
    ParsedAction result;
    result.value = common::PlayerAction{action_type, *tile};
    return result;
}

std::string_view operation_name(LobbyOperation operation) noexcept {
    switch (operation) {
        case LobbyOperation::JoinRoom: return "join_room";
        case LobbyOperation::Ready: return "ready";
        case LobbyOperation::Snapshot: return "snapshot";
    }
    return "unknown";
}

std::string_view action_name(common::PlayerActionType type) noexcept {
    switch (type) {
        case common::PlayerActionType::Tsumo: return "tsumo";
        case common::PlayerActionType::Ron: return "ron";
        case common::PlayerActionType::Pon: return "pon";
        case common::PlayerActionType::SelfKan: return "self_kan";
        case common::PlayerActionType::AddKan: return "add_kan";
        case common::PlayerActionType::OpenKan: return "open_kan";
        case common::PlayerActionType::Pass: return "pass";
        case common::PlayerActionType::Discard: return "discard";
        case common::PlayerActionType::None: return {};
    }
    return {};
}

std::string_view meld_name(common::MeldType type) noexcept {
    switch (type) {
        case common::MeldType::Pon: return "pon";
        case common::MeldType::OpenKan: return "open_kan";
        case common::MeldType::SelfKan: return "self_kan";
        case common::MeldType::AddKan: return "add_kan";
    }
    return {};
}

std::string_view table_stage_name(common::TableStage stage) noexcept {
    switch (stage) {
        case common::TableStage::WaitingReady: return "waiting_ready";
        case common::TableStage::Playing: return "playing";
        case common::TableStage::RoundFinished: return "round_finished";
    }
    return {};
}

std::string_view round_stage_name(common::RoundStage stage) noexcept {
    switch (stage) {
        case common::RoundStage::NotActive: return "not_active";
        case common::RoundStage::WaitingDraw: return "waiting_draw";
        case common::RoundStage::WaitingDiscard: return "waiting_discard";
        case common::RoundStage::WaitingClaim: return "waiting_claim";
        case common::RoundStage::Ended: return "ended";
    }
    return {};
}

std::string_view win_type_name(common::WinType type) noexcept {
    switch (type) {
        case common::WinType::NoWinner: return "no_winner";
        case common::WinType::Ron: return "ron";
        case common::WinType::Tsumo: return "tsumo";
    }
    return {};
}

std::string_view win_detail_name(common::WinDetail detail) noexcept {
    switch (detail) {
        case common::WinDetail::NoWinner: return "no_winner";
        case common::WinDetail::Simple: return "simple";
        case common::WinDetail::TsumoFromKan: return "tsumo_from_kan";
        case common::WinDetail::RonKanDiscard: return "ron_kan_discard";
        case common::WinDetail::RonAddKan: return "ron_add_kan";
    }
    return {};
}

std::string_view discard_detail_name(common::DiscardDetail detail) noexcept {
    switch (detail) {
        case common::DiscardDetail::None: return "none";
        case common::DiscardDetail::SimpleDraw: return "simple_draw";
        case common::DiscardDetail::AfterPon: return "after_pon";
        case common::DiscardDetail::AfterOpenKanDraw: return "after_open_kan_draw";
        case common::DiscardDetail::AfterSelfKanDraw: return "after_self_kan_draw";
        case common::DiscardDetail::AfterAddKanDraw: return "after_add_kan_draw";
    }
    return {};
}

std::string_view event_name(common::RoundEventType type) noexcept {
    switch (type) {
        case common::RoundEventType::RoundStarted: return "round_started";
        case common::RoundEventType::InitialHands: return "initial_hands";
        case common::RoundEventType::PlayerDraw: return "player_draw";
        case common::RoundEventType::PlayerDiscard: return "player_discard";
        case common::RoundEventType::ClaimSubmitted: return "claim_submitted";
        case common::RoundEventType::MeldDeclared: return "meld_declared";
        case common::RoundEventType::AddKanProposed: return "add_kan_proposed";
        case common::RoundEventType::PlayerWin: return "player_win";
        case common::RoundEventType::RoundEnded: return "round_ended";
        case common::RoundEventType::PointsCalculated: return "points_calculated";
        case common::RoundEventType::ChickenRevealed: return "chicken_revealed";
    }
    return {};
}

Json encode_seat(int seat) { return seat >= 0 && seat < 4 ? Json(seat) : Json(nullptr); }

Json encode_tile(common::Tile tile) {
    return common::tile_index(tile) >= 0 ? Json(common::tile_to_string(tile)) : Json(nullptr);
}

Json encode_tile(const std::optional<common::Tile>& tile) {
    return tile ? encode_tile(*tile) : Json(nullptr);
}

Json encode_action(const common::PlayerAction& action) {
    const auto name = action_name(action.type);
    if (name.empty()) return nullptr;
    Json result{{"type", std::string(name)}};
    switch (action.type) {
        case common::PlayerActionType::Pon:
        case common::PlayerActionType::SelfKan:
        case common::PlayerActionType::AddKan:
        case common::PlayerActionType::OpenKan:
        case common::PlayerActionType::Discard:
            result["tile"] = encode_tile(action.action_tile);
            break;
        default: break;
    }
    return result;
}

Json encode_tiles(const std::vector<common::Tile>& tiles) {
    auto result = Json::array();
    for (const auto tile : tiles) result.push_back(encode_tile(tile));
    return result;
}

Json encode_meld(const common::Meld& meld) {
    return Json{
        {"type", std::string(meld_name(meld.type))},
        {"tile", encode_tile(meld.tile)},
        {"fromSeat", meld.from_seat < 4 ? Json(meld.from_seat) : Json(nullptr)}
    };
}

Json encode_melds(const std::vector<common::Meld>& melds) {
    auto result = Json::array();
    for (const auto& meld : melds) result.push_back(encode_meld(meld));
    return result;
}

Json encode_dash_chicken(const std::optional<common::DashChicken>& chicken) {
    if (!chicken) return nullptr;
    return Json{
        {"tile", encode_tile(chicken->tile)},
        {"discardedBy", encode_seat(chicken->discarded_by)},
        {"claimedBy", encode_seat(chicken->claimed_by)},
        {"claimType", chicken->claimed_by >= 0
            ? Json(std::string(meld_name(chicken->claim_type))) : Json(nullptr)}
    };
}

Json encode_player_tile_state(const common::PlayerTileState& state) {
    return Json{
        {"hand", encode_tiles(state.hand)},
        {"river", encode_tiles(state.river)},
        {"melds", encode_melds(state.melds)},
        {"drawBuffer", encode_tile(state.draw_buffer)},
        {"discardBuffer", encode_tile(state.discard_buffer)}
    };
}

Json encode_round_result(const std::optional<common::RoundResult>& result) {
    if (!result) return nullptr;
    auto winners = Json::array();
    for (const int seat : result->winner_seats) winners.push_back(seat);
    auto states = Json::array();
    for (const auto& state : result->states) states.push_back(encode_player_tile_state(state));
    return Json{
        {"hasWinner", result->has_winner},
        {"winnerSeats", std::move(winners)},
        {"discarderSeat", encode_seat(result->discarder_seat)},
        {"winTile", encode_tile(result->win_tile)},
        {"winType", std::string(win_type_name(result->win_type))},
        {"winDetail", std::string(win_detail_name(result->detail))},
        {"states", std::move(states)},
        {"oneSou", encode_dash_chicken(result->one_sou)},
        {"eightPin", encode_dash_chicken(result->eight_pin)},
        {"roundChicken", encode_tile(result->round_chicken)},
        {"chickenIndicator", encode_tile(result->chicken_indicator)}
    };
}

Json encode_point_result(const std::optional<common::PointResult>& result) {
    if (!result) return nullptr;
    auto delta = Json::array();
    auto to_others = Json::array();
    auto detail = Json::array();
    for (int seat = 0; seat < 4; ++seat) {
        delta.push_back(std::to_string(result->delta_result[seat]));
        auto row = Json::array();
        for (int other = 0; other < 4; ++other) {
            row.push_back(std::to_string(result->point_to_others[seat][other]));
        }
        to_others.push_back(std::move(row));
        const auto& value = result->detail[seat];
        detail.push_back(Json{
            {"total", std::to_string(value.total_point)},
            {"chicken", std::to_string(value.point_from_chicken)},
            {"kan", std::to_string(value.point_from_kan)},
            {"agari", std::to_string(value.point_from_agari)},
            {"tenpai", std::to_string(value.point_from_tenpai)}
        });
    }
    return Json{{"delta", std::move(delta)}, {"toOthers", std::move(to_others)},
        {"detail", std::move(detail)}};
}

Json encode_event(const common::RoundEvent& event) {
    return Json{
        {"type", std::string(event_name(event.type))},
        {"seq", std::to_string(event.seq)},
        {"seat", encode_seat(event.player_seat)},
        {"fromSeat", encode_seat(event.from_seat)},
        {"tile", encode_tile(event.tile)},
        {"tiles", encode_tiles(event.tiles)},
        {"action", event.action ? encode_action(*event.action) : Json(nullptr)},
        {"discardDetail", std::string(discard_detail_name(event.discard_detail))}
    };
}

Json encode_events(const std::vector<common::RoundEvent>& events) {
    auto result = Json::array();
    for (const auto& event : events) {
        if (event.type != common::RoundEventType::ClaimSubmitted) {
            result.push_back(encode_event(event));
        }
    }
    return result;
}

} // namespace

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidJson: return "invalid_json";
        case ErrorCode::JsonTooDeep: return "json_too_deep";
        case ErrorCode::TextOnly: return "text_only";
        case ErrorCode::UnsupportedVersion: return "unsupported_version";
        case ErrorCode::InvalidType: return "invalid_type";
        case ErrorCode::UnknownType: return "unknown_type";
        case ErrorCode::InvalidField: return "invalid_field";
        case ErrorCode::InvalidTile: return "invalid_tile";
        case ErrorCode::InvalidAction: return "invalid_action";
        case ErrorCode::HelloRequired: return "hello_required";
        case ErrorCode::AlreadyAuthenticated: return "already_authenticated";
        case ErrorCode::InvalidNickname: return "invalid_nickname";
        case ErrorCode::InvalidSession: return "invalid_session";
        case ErrorCode::SessionExpired: return "session_expired";
        case ErrorCode::NotSeated: return "not_seated";
        case ErrorCode::RoomNotFound: return "room_not_found";
        case ErrorCode::RoomMismatch: return "room_mismatch";
        case ErrorCode::RoomFull: return "room_full";
        case ErrorCode::AlreadyInRoom: return "already_in_room";
        case ErrorCode::StaleRound: return "stale_round";
        case ErrorCode::StalePrompt: return "stale_prompt";
        case ErrorCode::StaleRequest: return "stale_request";
        case ErrorCode::ActionNotAllowed: return "action_not_allowed";
        case ErrorCode::RateLimited: return "rate_limited";
        case ErrorCode::InternalError: return "internal_error";
    }
    return "internal_error";
}

std::optional<std::uint64_t> parse_u64(const Json& value) noexcept {
    if (!value.is_string()) return std::nullopt;
    const auto& text = value.get_ref<const std::string&>();
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return result;
}

DecodeResult decode_client_message(std::string_view text) {
    if (text.size() > max_message_bytes) return fail(ErrorCode::InvalidJson, "message is too large");
    if (!check_json_depth(text)) return fail(ErrorCode::JsonTooDeep, "json nesting depth exceeds limit");

    Json root;
    try {
        root = Json::parse(text);
    } catch (const nlohmann::json::parse_error&) {
        return fail(ErrorCode::InvalidJson, "invalid json");
    }
    if (!root.is_object()) return fail(ErrorCode::InvalidJson, "root must be an object");

    const auto* wire_version = find_field(root, "v");
    if (!wire_version || !wire_version->is_number_integer() || *wire_version != version) {
        return fail(ErrorCode::UnsupportedVersion, "unsupported protocol version");
    }
    const auto type = required_string(root, "type", 1, 32);
    if (!type) return fail(ErrorCode::InvalidType, "type must be a string");

    if (*type == "hello") {
        if (!only_contains(root, {"v", "type", "nickname", "sessionToken"})) {
            return fail(ErrorCode::InvalidField, "unknown hello field");
        }
        const auto* nickname_json = find_field(root, "nickname");
        const auto* token_json = find_field(root, "sessionToken");
        if ((nickname_json == nullptr) == (token_json == nullptr)) {
            return fail(ErrorCode::InvalidField, "provide exactly one hello credential");
        }
        if (nickname_json) {
            if (!nickname_json->is_string()) return fail(ErrorCode::InvalidNickname, "nickname must be a string");
            auto nickname = trim_ascii_whitespace(nickname_json->get<std::string>());
            if (!valid_nickname(nickname)) return fail(ErrorCode::InvalidNickname, "invalid nickname");
            HelloRequest request;
            request.nickname = std::move(nickname);
            return succeed(std::move(request));
        }
        if (!token_json->is_string()) return fail(ErrorCode::InvalidSession, "sessionToken must be a string");
        auto token = token_json->get<std::string>();
        if (!valid_session_token(token)) return fail(ErrorCode::InvalidSession, "invalid sessionToken");
        HelloRequest request;
        request.session_token = std::move(token);
        return succeed(std::move(request));
    }

    if (*type == "ping") {
        if (!only_contains(root, {"v", "type", "nonce"})) return fail(ErrorCode::InvalidField, "unknown ping field");
        bool valid = true;
        auto nonce = optional_string(root, "nonce", 64, valid);
        if (!valid) return fail(ErrorCode::InvalidField, "invalid nonce");
        PingRequest request;
        request.nonce = std::move(nonce);
        return succeed(std::move(request));
    }

    const auto decode_room_request = [&](auto make_request) -> DecodeResult {
        if (!only_contains(root, {"v", "type", "requestTag", "roomId"})) {
            return fail(ErrorCode::InvalidField, "unknown room request field");
        }
        auto tag = required_string(root, "requestTag", 1, 64);
        auto room = required_string(root, "roomId", 1, 64);
        if (!tag || !room) return fail(ErrorCode::InvalidField, "invalid room request fields");
        return succeed(make_request(std::move(*tag), std::move(*room)));
    };

    if (*type == "join_room") {
        return decode_room_request([](std::string tag, std::string room) -> ClientRequest {
            return JoinRoomRequest{std::move(tag), std::move(room)};
        });
    }
    if (*type == "ready") {
        return decode_room_request([](std::string tag, std::string room) -> ClientRequest {
            return ReadyRequest{std::move(tag), std::move(room)};
        });
    }
    if (*type == "snapshot") {
        return decode_room_request([](std::string tag, std::string room) -> ClientRequest {
            return SnapshotRequest{std::move(tag), std::move(room)};
        });
    }

    if (*type == "action") {
        if (!only_contains(root,
                {"v", "type", "roomId", "roundId", "requestId", "promptId", "action"})) {
            return fail(ErrorCode::InvalidField, "unknown action field");
        }
        auto room = required_string(root, "roomId", 1, 64);
        const auto* round_json = find_field(root, "roundId");
        const auto* request_json = find_field(root, "requestId");
        const auto* prompt_json = find_field(root, "promptId");
        const auto* action_json = find_field(root, "action");
        if (!room || !round_json || !request_json || !prompt_json || !action_json) {
            return fail(ErrorCode::InvalidField, "missing action field");
        }
        const auto round_id = parse_u64(*round_json);
        const auto request_id = parse_u64(*request_json);
        const auto prompt_id = parse_u64(*prompt_json);
        if (!round_id || !request_id || !prompt_id || *request_id == 0) {
            return fail(ErrorCode::InvalidField, "invalid action identifier");
        }
        const auto action = parse_action(*action_json);
        if (!action.value) {
            return fail(action.error, action.error == ErrorCode::InvalidTile ? "invalid tile" : "invalid action");
        }
        ActionRequest request;
        request.room_id = std::move(*room);
        request.command = common::Command{*round_id, *request_id, *prompt_id, *action.value};
        return succeed(std::move(request));
    }

    return fail(ErrorCode::UnknownType, "unknown message type");
}

Json encode_welcome(const Welcome& value) {
    return Json{{"v", version}, {"type", "welcome"}, {"sessionToken", value.session_token},
        {"resumed", value.resumed}, {"nickname", value.nickname},
        {"roomId", value.room_id ? Json(*value.room_id) : Json(nullptr)}};
}

Json encode_pong(const PingRequest& request) {
    Json result{{"v", version}, {"type", "pong"}};
    if (request.nonce) result["nonce"] = *request.nonce;
    return result;
}

Json encode_ack(const LobbyAck& value) {
    return Json{{"v", version}, {"type", "ack"},
        {"operation", std::string(operation_name(value.operation))},
        {"requestTag", value.request_tag}, {"accepted", value.accepted},
        {"error", value.error ? Json(std::string(to_string(*value.error))) : Json(nullptr)}};
}

Json encode_ack(const ActionAck& value) {
    return Json{{"v", version}, {"type", "ack"}, {"operation", "action"},
        {"requestId", std::to_string(value.request_id)}, {"accepted", value.accepted},
        {"duplicate", value.duplicate}, {"tableSeq", std::to_string(value.table_seq)},
        {"error", value.error ? Json(std::string(to_string(*value.error))) : Json(nullptr)}};
}

Json encode_error(const ProtocolError& value) {
    Json result{{"v", version}, {"type", "error"},
        {"code", std::string(to_string(value.code))}, {"message", value.message}};
    if (value.request_tag) result["requestTag"] = *value.request_tag;
    if (value.request_id) result["requestId"] = std::to_string(*value.request_id);
    return result;
}

Json encode_notice(const ServerNotice& value) {
    return Json{{"v", version}, {"type", "server_notice"},
        {"code", value.code}, {"message", value.message}};
}

Json encode_table_update(
    std::string_view room_id,
    const common::Delivery& delivery,
    common::TableClock::time_point now)
{
    const auto& view = delivery.view;
    auto seats = Json::array();
    for (const auto& seat : view.seats) {
        seats.push_back(Json{{"occupied", seat.occupied}, {"connected", seat.connected},
            {"ready", seat.ready}, {"nickname", seat.player_name}, {"handSize", seat.hand_size},
            {"river", encode_tiles(seat.river)}, {"melds", encode_melds(seat.melds)}});
    }
    auto actions = Json::array();
    for (const auto& action : view.available_actions) actions.push_back(encode_action(action));
    auto points = Json::array();
    for (const auto value : view.total_points) points.push_back(std::to_string(value));

    Json remaining_ms = nullptr;
    if (view.deadline) {
        remaining_ms = *view.deadline > now
            ? std::chrono::duration_cast<std::chrono::milliseconds>(*view.deadline - now).count()
            : 0;
    }

    Json encoded_view{
        {"selfSeat", view.self_seat},
        {"tableStage", std::string(table_stage_name(view.table_stage))},
        {"roundStage", std::string(round_stage_name(view.round_stage))},
        {"tableSeq", std::to_string(view.table_seq)},
        {"roundId", std::to_string(view.round_id)},
        {"roundSeq", std::to_string(view.round_seq)},
        {"promptId", std::to_string(view.prompt_id)},
        {"lastRequestId", std::to_string(view.last_request_id)},
        {"actingSeat", encode_seat(view.acting_player)},
        {"dealerSeat", view.dealer_seat},
        {"tilesRemaining", view.tiles_remaining},
        {"remainingMs", std::move(remaining_ms)},
        {"seats", std::move(seats)},
        {"ownTiles", Json{{"hand", encode_tiles(view.own_tiles.hand)},
            {"drawBuffer", encode_tile(view.own_tiles.draw_buffer)},
            {"discardBuffer", encode_tile(view.own_tiles.discard_buffer)}}},
        {"availableActions", std::move(actions)},
        {"totalPoints", std::move(points)},
        {"oneSou", encode_dash_chicken(view.one_sou)},
        {"eightPin", encode_dash_chicken(view.eight_pin)},
        {"pendingAction", view.pending_action ? encode_action(*view.pending_action) : Json(nullptr)},
        {"roundResult", encode_round_result(view.round_result)},
        {"pointResult", encode_point_result(view.point_result)}
    };

    return Json{{"v", version}, {"type", "table_update"},
        {"roomId", std::string(room_id)}, {"view", std::move(encoded_view)},
        {"events", encode_events(delivery.events)}};
}

} // namespace gymj::server::gateway::protocol
