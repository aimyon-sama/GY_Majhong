#include <gymj/server/gateway/protocol.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

namespace protocol = gymj::server::gateway::protocol;
namespace common = gymj::common;
using Json = nlohmann::json;

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

protocol::ErrorCode error_code(const protocol::DecodeResult& result) {
    require(!result && result.error.has_value(), "expected protocol error");
    return result.error->code;
}

void test_integer_parser() {
    require(protocol::parse_u64(Json("0")) == 0, "zero should parse");
    require(protocol::parse_u64(Json("18446744073709551615")).has_value(),
        "uint64 max should parse");
    for (const Json value : {Json(1), Json(""), Json("01"), Json("+1"), Json("-1"),
             Json("18446744073709551616")}) {
        require(!protocol::parse_u64(value), "non-canonical uint64 should fail");
    }
}

void test_hello_and_room_requests() {
    auto hello = protocol::decode_client_message(
        R"({"v":1,"type":"hello","nickname":"  玩家甲  "})");
    require(static_cast<bool>(hello), "valid hello should decode");
    const auto& hello_request = std::get<protocol::HelloRequest>(*hello.request);
    require(hello_request.nickname == "玩家甲", "nickname should be trimmed");
    require(!hello_request.session_token, "new hello should not contain token");

    require(error_code(protocol::decode_client_message(
        R"({"v":1,"type":"hello","nickname":"a","sessionToken":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"))
        == protocol::ErrorCode::InvalidField, "hello credentials must be exclusive");
    require(error_code(protocol::decode_client_message(
        R"({"v":1,"type":"hello","sessionToken":"ABC"})"))
        == protocol::ErrorCode::InvalidSession, "invalid token should fail");
    require(error_code(protocol::decode_client_message(
        R"({"version":1,"type":"ping"})"))
        == protocol::ErrorCode::UnsupportedVersion, "legacy version field should fail");

    auto join = protocol::decode_client_message(
        R"({"v":1,"type":"join_room","requestTag":"join-1","roomId":"room-001"})");
    require(join && std::holds_alternative<protocol::JoinRoomRequest>(*join.request),
        "join_room should decode");
}

void test_action_decoder() {
    auto decoded = protocol::decode_client_message(
        R"({"v":1,"type":"action","roomId":"room-001","roundId":"2","requestId":"7","promptId":"11","action":{"type":"discard","tile":"5m"}})");
    require(static_cast<bool>(decoded), "valid action should decode");
    const auto& request = std::get<protocol::ActionRequest>(*decoded.request);
    require(request.room_id == "room-001", "action room mismatch");
    require(request.command.round_id == 2 && request.command.request_id == 7
        && request.command.prompt_id == 11, "action identifiers mismatch");
    require(request.command.action.type == common::PlayerActionType::Discard,
        "action type mismatch");
    require(request.command.action.action_tile == common::Tile{common::TileType::Man, 5},
        "action tile mismatch");

    require(error_code(protocol::decode_client_message(
        R"({"v":1,"type":"action","roomId":"room-001","roundId":"2","requestId":"7","promptId":"11","action":{"type":"discard","tile":"0m"}})"))
        == protocol::ErrorCode::InvalidTile, "invalid tile should fail");
    require(error_code(protocol::decode_client_message(
        R"({"v":1,"type":"action","roomId":"room-001","roundId":"2","requestId":"7","promptId":"11","action":{"type":"pass","tile":"5m"}})"))
        == protocol::ErrorCode::InvalidAction, "pass with tile should fail");
    require(error_code(protocol::decode_client_message(
        R"({"v":1,"type":"action","roomId":"room-001","command":{}})"))
        == protocol::ErrorCode::InvalidField, "legacy nested command should fail");
}

void test_server_encoders() {
    protocol::Welcome welcome_value;
    welcome_value.session_token = std::string(64, 'a');
    welcome_value.nickname = "玩家甲";
    const auto welcome = protocol::encode_welcome(welcome_value);
    require(welcome.at("v") == 1 && welcome.at("roomId").is_null(),
        "welcome envelope mismatch");
    require(!welcome.contains("session_token"), "welcome must use camelCase fields");

    protocol::ActionAck action_ack_value;
    action_ack_value.request_id = 7;
    action_ack_value.accepted = true;
    action_ack_value.table_seq = 31;
    const auto action_ack = protocol::encode_ack(action_ack_value);
    require(action_ack.at("type") == "ack" && action_ack.at("operation") == "action",
        "action ack shape mismatch");
    require(action_ack.at("requestId") == "7" && action_ack.at("error").is_null(),
        "action ack fields mismatch");

    const auto notice = protocol::encode_notice({"replay_write_failed", "failed"});
    require(notice.at("type") == "server_notice", "notice type mismatch");
}

void test_table_update_encoder() {
    const auto now = common::TableClock::time_point(std::chrono::seconds(100));
    common::Delivery delivery{};
    delivery.recipient_id = 987654321;
    auto& view = delivery.view;
    view.self_seat = 0;
    view.table_stage = common::TableStage::Playing;
    view.round_stage = common::RoundStage::WaitingDiscard;
    view.table_seq = 31;
    view.round_id = 2;
    view.round_seq = 18;
    view.prompt_id = 11;
    view.last_request_id = 6;
    view.acting_player = 0;
    view.dealer_seat = 1;
    view.tiles_remaining = 55;
    view.deadline = now + std::chrono::milliseconds(1500);
    view.total_points = {10, -4, -3, -3};
    view.seats[0].occupied = true;
    view.seats[0].connected = true;
    view.seats[0].player_name = "玩家甲";
    view.seats[0].hand_size = 14;
    view.seats[0].river.push_back({common::TileType::Pin, 9});
    view.own_tiles.hand.push_back({common::TileType::Man, 1});
    view.own_tiles.draw_buffer = common::Tile{common::TileType::Man, 5};
    view.available_actions.push_back({
        common::PlayerActionType::Discard,
        common::Tile{common::TileType::Man, 5}
    });
    common::RoundResult round_result{};
    round_result.has_winner = true;
    round_result.winner_seats = {0};
    round_result.winner_seat = 0;
    round_result.win_type = common::WinType::Tsumo;
    round_result.detail = common::WinDetail::Simple;
    round_result.win_tile = {common::TileType::Man, 5};
    round_result.states[0].hand.push_back({common::TileType::Sou, 9});
    view.round_result = round_result;

    common::RoundEvent draw_event;
    draw_event.type = common::RoundEventType::PlayerDraw;
    draw_event.seq = 18;
    draw_event.player_seat = 0;
    draw_event.tile = common::Tile{common::TileType::Man, 5};
    draw_event.discard_detail = common::DiscardDetail::SimpleDraw;
    delivery.events.push_back(draw_event);

    common::RoundEvent claim_event;
    claim_event.type = common::RoundEventType::ClaimSubmitted;
    claim_event.seq = 19;
    claim_event.player_seat = 1;
    delivery.events.push_back(claim_event);

    const auto encoded = protocol::encode_table_update("room-001", delivery, now);
    require(encoded.at("v") == 1 && encoded.at("type") == "table_update",
        "table update envelope mismatch");
    require(encoded.at("roomId") == "room-001" && !encoded.contains("recipient_id")
        && !encoded.contains("payload"), "table update leaked internal shape");

    const auto& encoded_view = encoded.at("view");
    require(encoded_view.at("tableSeq") == "31", "tableSeq must be a string");
    require(encoded_view.at("remainingMs") == 1500, "remaining time mismatch");
    require(encoded_view.at("ownTiles").at("drawBuffer") == "5m",
        "private draw tile mismatch");
    require(encoded_view.at("totalPoints").at(1) == "-4", "points must be strings");
    require(encoded_view.at("roundResult").at("states").size() == 4
        && encoded_view.at("roundResult").at("states").at(0).at("hand").at(0) == "9s",
        "round result must expose all final tile states");
    require(encoded.at("events").size() == 1
        && encoded.at("events").at(0).at("type") == "player_draw",
        "private claim event must not be encoded");
}

} // namespace

int main() {
    try {
        test_integer_parser();
        test_hello_and_room_requests();
        test_action_decoder();
        test_server_encoders();
        test_table_update_encoder();
        std::cout << "protocol tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "protocol test failed: " << error.what() << '\n';
        return 1;
    }
}
