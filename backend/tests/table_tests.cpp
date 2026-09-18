#include <gymj/core/room/table.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {
using namespace gymj::common;
using gymj::room::Table;
using gymj::room::Wall;
using namespace std::chrono_literals;

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::vector<nlohmann::json> read_replay(const Table& table) {
    require(table.replay().error().empty(), "replay must have no storage errors");
    std::ifstream file(table.replay().path(), std::ios::binary);
    require(file.is_open(), "replay file exists while table is alive");
    std::vector<nlohmann::json> entries;
    std::string line;
    while (std::getline(file, line)) entries.push_back(nlohmann::json::parse(line));
    require(!file.bad(), "replay read succeeds");
    return entries;
}

Tile m(int rank) { return {TileType::Man, static_cast<std::uint8_t>(rank)}; }
Tile s(int rank) { return {TileType::Sou, static_cast<std::uint8_t>(rank)}; }
Tile p(int rank) { return {TileType::Pin, static_cast<std::uint8_t>(rank)}; }

std::array<Tile, Wall::tile_count> wall_with(
    const std::array<std::vector<Tile>, 4>& hands,
    const std::vector<Tile>& draws = {}, const std::vector<Tile>& kan_draws = {}) {
    // 指定关键手牌和摸牌位置，其余位置补齐为合法的 108 张牌山。
    std::array<Tile, Wall::tile_count> tiles;
    tiles.fill(null_tile);
    std::array<int, tileKindCount> counts{};
    auto place = [&](int index, Tile tile) {
        require(tiles[index] == null_tile, "fixture overlap");
        require(++counts[tile_index(tile)] <= 4, "fixture overuses tile");
        tiles[index] = tile;
    };
    for (int seat = 0; seat < 4; ++seat) {
        for (std::size_t i = 0; i < hands[seat].size(); ++i) place(static_cast<int>(i) * 4 + seat, hands[seat][i]);
    }
    for (std::size_t i = 0; i < draws.size(); ++i) place(52 + static_cast<int>(i), draws[i]);
    for (std::size_t i = 0; i < kan_draws.size(); ++i) {
        place(106 - static_cast<int>(i / 2) * 2 + static_cast<int>(i % 2), kan_draws[i]);
    }
    int index = 0;
    for (auto& tile : tiles) {
        if (tile == null_tile) {
            while (counts[index] == 4) ++index;
            tile = tile_from_index(index);
            ++counts[index];
        }
    }
    return tiles;
}

struct Fixture {
    // 同名玩家、固定种子和可控时钟用于验证身份隔离及超时边界。
    Table table{RuleConfig{}, TableOptions{0, 100, 42}};
    std::array<PlayerInfo, 4> players;
    std::array<std::uint64_t, 4> requests{};
    Table::TimePoint now{};

    Fixture() {
        for (auto& player : players) {
            auto joined = table.join(PlayerInfo{"same name"});
            require(joined.accepted && joined.assigned_player.has_value(), "join assigns identity");
            player = *joined.assigned_player;
            require(table.ready(player).accepted, "ready should succeed");
        }
    }

    PlayerView view(int seat = 0) { return *table.snapshot(players[seat]); }
    Command command(int seat, PlayerActionType type, Tile tile) {
        const auto current = view(seat);
        return {current.round_id, ++requests[seat], current.prompt_id, {type, tile}};
    }
    TableUpdate act(int seat, PlayerActionType type, Tile tile) {
        auto result = table.submit(players[seat], command(seat, type, tile), now);
        require(result.accepted && result.error.empty(), "action rejected: " + result.error);
        return result;
    }
    void pass_remaining() {
        for (int seat = 0; seat < 4 && view().round_stage == RoundStage::WaitingClaim; ++seat) {
            for (const auto action : view(seat).available_actions) {
                if (action.type == PlayerActionType::Pass) {
                    act(seat, action.type, action.action_tile);
                    break;
                }
            }
        }
        require(view().round_stage != RoundStage::WaitingClaim, "claims should resolve");
    }
};

void test_identity_and_lifecycle() {
    Table table{RuleConfig{}};
    require(!table.start().accepted, "empty table cannot start");
    require(!table.ready(PlayerInfo{}).accepted, "empty identity must not match empty seat");
    require(!table.submit(PlayerInfo{}, {}).accepted, "submit before start must not dereference null");
    require(!table.snapshot(PlayerInfo{}), "unknown identity has no private snapshot");
    require(!table.join(PlayerInfo{"forged", 123456}).accepted, "unknown nonzero identity rejected");
    require(table.tick().deliveries.empty(), "idle tick has no updates");

    Fixture f;
    for (int i = 0; i < 4; ++i) {
        require(f.players[i].id != 0, "server identity is nonzero");
        for (int j = 0; j < i; ++j) require(!(f.players[i] == f.players[j]), "same names must not share identity");
        require(f.view(i).self_seat == i, "same names must occupy distinct seats");
    }
    require(!f.table.join(PlayerInfo{"fifth"}).accepted, "full table rejects another join");
    const auto before = f.view().table_seq;
    require(f.table.join(f.players[0]).accepted && f.view().table_seq == before, "known join is idempotent");
    require(f.table.disconnect(f.players[1]).accepted, "disconnect known player");
    require(!f.table.start(f.now).accepted, "disconnected player prevents start");
    require(!f.table.ready(f.players[1]).accepted, "offline ready rejected");
    require(f.table.join(f.players[1]).accepted, "same identity reconnects");
    require(!f.table.start(f.now).accepted, "disconnect cleared readiness");
    f.table.ready(f.players[1]);
    require(f.table.start(f.now).accepted, "four ready players start");
    require(f.view().round_stage == RoundStage::WaitingDiscard, "start automatically draws for dealer");
    require(f.view().own_tiles.hand.size() == 14, "dealer has fourteen tiles");
    const auto seq = f.view().table_seq;
    require(!f.table.start(f.now).accepted && f.view().table_seq == seq, "active round cannot be replaced");
    require(!f.table.ready(f.players[0]).accepted, "ready during play rejected");
    auto renamed = f.players[0];
    renamed.player_name = "different";
    require(f.table.snapshot(renamed)->self_seat == 0, "identity is independent of nickname");
    require(f.view().seats[0].player_name == "same name", "lookup cannot rename player");
}

void test_start_and_visibility() {
    Fixture f;
    const auto started = f.table.start(f.now);
    require(started.accepted && started.deliveries.size() == 8, "start and draw each reach four players");
    const auto log = read_replay(f.table);
    require(log.size() == 4 && log[0]["type"] == "table" && log[0]["version"] == 1,
        "one header, round metadata, initial deal and dealer draw are flushed");
    require(log[1]["round_id"] == 1 && log[1]["players"].size() == 4, "round metadata recorded");
    require(log[2]["events"].size() == 5, "initial events recorded once, not once per recipient");
    for (int seat = 0; seat < 4; ++seat) {
        require(log[2]["events"][seat + 1]["tiles"].size() == 13, "all initial hands retained in replay");
    }
    require(log[3]["events"][0]["tile"] == tile_to_string(*f.view().own_tiles.draw_buffer),
        "authoritative dealer draw retained");
    for (const auto& delivery : started.deliveries) {
        require(delivery.recipient_id == f.players[delivery.view.self_seat].id, "routing maps to identity");
        require(!delivery.view.round_result, "live snapshot has no full result");
        for (const auto& event : delivery.events) {
            if (event.player_seat == delivery.view.self_seat) {
                if (event.type == RoundEventType::InitialHands) {
                    require(event.tiles.size() == 13, "owner receives initial hand");
                }
                if (event.type == RoundEventType::PlayerDraw) {
                    require(event.tile == delivery.view.own_tiles.draw_buffer, "owner receives drawn tile");
                }
                continue;
            }
            if (event.type == RoundEventType::InitialHands) require(event.tiles.empty(), "opponent initial hand hidden");
            if (event.type == RoundEventType::PlayerDraw) require(!event.tile, "opponent draw hidden");
        }
    }
    Fixture same_seed;
    require(same_seed.table.start(same_seed.now).accepted, "second seeded table starts");
    for (int seat = 0; seat < 4; ++seat) {
        require(same_seed.view(seat).own_tiles.hand == f.view(seat).own_tiles.hand,
            "same seed produces the same initial hands");
    }
}

void test_request_validation_and_retries() {
    Fixture f;
    f.table.start(f.now);
    const auto before = f.view();
    auto invalid = f.command(0, PlayerActionType::Discard, null_tile);
    auto rejected = f.table.submit(f.players[0], invalid, f.now);
    require(!rejected.accepted, "illegal tile rejected");
    require(rejected.deliveries.empty() && f.view().table_seq == before.table_seq
        && f.view().own_tiles.hand == before.own_tiles.hand, "rejected action does not change game state");
    require(f.table.submit(f.players[0], invalid, f.now).duplicate, "rejected request also cached");

    const auto command = f.command(0, PlayerActionType::Discard, *f.view().own_tiles.draw_buffer);
    const auto first = f.table.submit(f.players[0], command, f.now);
    require(first.accepted, "valid discard accepted");
    require(f.view().last_request_id == command.request_id, "reconnect snapshot exposes request watermark");
    for (const auto& delivery : first.deliveries) {
        if (delivery.view.self_seat == 0) require(delivery.view.last_request_id == command.request_id,
            "command delivery includes current request watermark");
    }
    const auto after = f.view();
    const auto seq = f.view().table_seq;
    const auto retry = f.table.submit(f.players[0], command, f.now + 1s);
    require(retry.accepted && retry.duplicate && retry.deliveries.empty(), "retry is receipt only even after deadline");
    require(retry.table_seq == first.table_seq && f.view().table_seq == seq, "retry doesn't advance state");
    require(f.view().round_seq == after.round_seq && f.view().own_tiles.hand == after.own_tiles.hand
        && f.view().own_tiles.river == after.own_tiles.river, "retry does not apply the discard twice");
    auto conflicting = command;
    conflicting.action.action_tile = null_tile;
    require(!f.table.submit(f.players[0], conflicting, f.now).accepted, "request ID cannot change contents");
    auto stale = f.command(0, PlayerActionType::Discard, m(1));
    stale.round_id = 99;
    require(!f.table.submit(f.players[0], stale, f.now).accepted, "wrong round rejected");
    stale.round_id = f.view().round_id;
    stale.prompt_id = 0;
    require(!f.table.submit(f.players[0], stale, f.now).accepted, "wrong prompt rejected");
}

std::array<std::vector<Tile>, 4> multi_ron_hands() {
    std::array<std::vector<Tile>, 4> hands{};
    hands[1] = hands[2] = {m(1), m(2), m(3), m(1), m(2), m(3), m(6), m(7), m(8), m(6), m(7), m(8), m(5)};
    return hands;
}

void test_parallel_claims_and_settlement() {
    // 模拟两人几乎同时提交荣和，验证先到达的响应不会使另一人的窗口失效。
    Fixture f;
    const auto tiles = wall_with(multi_ron_hands(), {m(5), s(5)});
    require(f.table.start(tiles, f.now).accepted, "fixture starts");
    f.act(0, PlayerActionType::Discard, m(5));
    const auto second = f.command(2, PlayerActionType::Ron, m(5));
    const auto first = f.command(1, PlayerActionType::Ron, m(5));
    const auto deadline = f.view(1).deadline;
    f.now += 10ms;
    const auto claimed = f.table.submit(f.players[2], second, f.now);
    require(claimed.accepted, "first arriving ron accepted");
    require(f.view(1).prompt_id == first.prompt_id && f.view(1).deadline == deadline,
        "other claim keeps window and original deadline");
    require(!f.view(2).deadline && f.view(2).available_actions.empty(), "responded player no longer prompted");
    for (const auto& delivery : claimed.deliveries) {
        if (delivery.view.self_seat == 2) continue;
        for (const auto& event : delivery.events) require(event.type != RoundEventType::ClaimSubmitted,
            "pending choices hidden from opponents");
    }
    require(f.table.submit(f.players[1], first, f.now).accepted, "same-window response survives sequence change");
    f.pass_remaining();
    auto view = f.view();
    require(view.table_stage == TableStage::RoundFinished && view.point_result, "win automatically settles");
    require(view.round_result->winner_seats == std::vector<int>({1, 2}), "both winners retained");
    const auto log = read_replay(f.table);
    const auto& finished = log.back();
    require(finished["type"] == "round_finished" && finished["round_result"]["winner_seats"]
        == std::vector<int>({1, 2}), "replay retains multi-ron winners");
    require(finished["point_result"]["delta_result"] == view.point_result->delta_result
        && finished["point_result"]["point_to_others"] == view.point_result->point_to_others
        && finished["total_points"] == view.total_points, "replay records full settlement and totals");
    require(finished["round_result"]["chicken_indicator"]
        == tile_to_string(*view.round_result->chicken_indicator), "revealed chicken retained");
    bool recorded_claim = false;
    for (const auto& entry : log) {
        if (!entry.contains("events")) continue;
        for (const auto& event : entry["events"]) {
            if (event["type"] == "ClaimSubmitted") recorded_claim = true;
        }
    }
    require(recorded_claim, "unfiltered claim choices retained");
    require(view.total_points[1] > 0 && view.total_points[2] > 0, "winning points accumulated");
    require(std::accumulate(view.total_points.begin(), view.total_points.end(), std::int64_t{0}) == 0,
        "total scores zero-sum");
    const auto settled_seq = view.table_seq;
    require(f.table.submit(f.players[1], first, f.now).duplicate, "winning request retries after settlement");
    require(f.table.tick(f.now + 1s).deliveries.empty(), "settled tick does nothing");
    require(read_replay(f.table) == log, "duplicate winning request and idle tick do not append");
    require(f.view().table_seq == settled_seq && f.view().total_points == view.total_points,
        "settlement only counted once");
    for (int seat = 0; seat < 4; ++seat) {
        require(view.total_points[seat] == view.point_result->delta_result[seat],
            "first round totals equal the single settlement delta");
    }
    require(!f.table.start(f.now).accepted, "next round requires fresh readiness");
    for (const auto& player : f.players) f.table.ready(player);
    require(f.table.start(f.now + 1s).accepted, "next round starts after ready");
    require(f.view().round_id == 2 && f.view().total_points == view.total_points, "next round retains cumulative points");
    require(!f.table.submit(f.players[1], first, f.now + 1s).accepted, "previous round command rejected");
}

void test_disconnect_and_timeout_windows() {
    // 截止时刻拒绝迟到命令，重连不续时，新窗口不能被旧 tick 立即超时处理。
    Fixture f;
    f.table.start(wall_with(multi_ron_hands(), {m(5), s(5)}), f.now);
    auto deadline = f.view().deadline;
    f.table.disconnect(f.players[0]);
    require(!f.table.submit(f.players[0], f.command(0, PlayerActionType::Discard, m(5)), f.now).accepted,
        "disconnected player cannot act");
    const auto rejoined = f.table.join(f.players[0]);
    require(rejoined.assigned_player->id == f.players[0].id && f.view().deadline == deadline,
        "reconnect retains identity and original deadline");
    require(f.table.tick(*deadline - 1ms).deliveries.empty(), "no early timeout");
    auto late = f.command(0, PlayerActionType::Discard, m(5));
    require(!f.table.submit(f.players[0], late, *deadline).accepted, "deadline boundary rejects late command");
    require(f.table.tick(*deadline).accepted, "timeout discards automatically");
    require(f.view().round_stage == RoundStage::WaitingClaim, "timeout discard opens claim window");
    require(f.table.tick(*deadline).deliveries.empty(), "old tick cannot expire fresh window");
    const auto claim_deadline = f.view(1).deadline;
    require(claim_deadline == *deadline + 100ms, "new window gets full timeout");
    f.table.disconnect(f.players[1]);
    const auto expired = f.table.tick(*claim_deadline);
    require(expired.accepted && f.view().round_stage == RoundStage::WaitingDiscard, "all expired claims pass");
    require(f.view(1).deadline == *claim_deadline + 100ms, "new actor not timed out in old loop");
    for (const auto& delivery : expired.deliveries) require(delivery.recipient_id != f.players[1].id,
        "offline player receives no delivery");
}

void test_concealed_kan_visibility() {
    Fixture f;
    std::array<std::vector<Tile>, 4> hands{};
    hands[0] = {s(9), s(9), s(9), m(1), m(2), m(3), s(4), s(5), s(6), p(1), p(2), p(3), m(9)};
    f.table.start(wall_with(hands, {s(9)}, {m(9)}), f.now);
    const auto result = f.act(0, PlayerActionType::SelfKan, s(9));
    require(f.view().own_tiles.melds[0].tile == s(9), "owner sees concealed kan");
    require(f.view(1).seats[0].melds[0].tile == null_tile, "opponent snapshot hides concealed kan");
    require(f.view().own_tiles.draw_buffer == m(9), "kan replacement draw automatic");
    const auto log = read_replay(f.table);
    require(log[log.size() - 2]["events"][0]["type"] == "MeldDeclared"
        && log[log.size() - 2]["events"][0]["tile"] == "9s"
        && log[log.size() - 2]["events"][0]["action"]["type"] == "SelfKan",
        "replay retains concealed kan tile and action");
    require(log.back()["events"][0]["tile"] == "9m", "replay retains replacement draw");
    for (const auto& delivery : result.deliveries) {
        if (delivery.view.self_seat == 0) continue;
        for (const auto& event : delivery.events) {
            if (event.type == RoundEventType::MeldDeclared) {
                require(!event.tile && event.action->action_tile == null_tile, "concealed kan event filtered");
            }
            if (event.type == RoundEventType::PlayerDraw) require(!event.tile, "replacement tile filtered");
        }
    }
}

void test_failed_start_is_transactional() {
    Fixture f;
    const auto before = read_replay(f.table);
    const auto seq = f.view().table_seq;
    auto tiles = wall_with({});
    tiles[0] = null_tile;
    require(!f.table.start(tiles, f.now).accepted, "invalid wall rejected");
    require(read_replay(f.table) == before, "rejected start adds no replay round");
    require(f.view().round_id == 0 && f.view().table_seq == seq
        && f.view().round_stage == RoundStage::NotActive,
        "failed start leaves no partial round");
    require(f.view().seats[0].ready, "failed start retains readiness");
    require(f.table.start(f.now).accepted, "valid retry can start");
}

void test_options_and_ids_across_tables() {
    Fixture first;
    Table other{RuleConfig{}, TableOptions{3, 250, 0}};
    require(other.replay().path() != first.table.replay().path(), "tables own distinct replay files");
    std::array<PlayerInfo, 4> players;
    for (auto& player : players) {
        player = *other.join(PlayerInfo{"same name"}).assigned_player;
        for (const auto& previous : first.players) require(player.id != previous.id, "IDs unique across tables");
        other.ready(player);
    }
    require(other.start(Table::TimePoint{}).accepted, "configured table starts");
    const auto view = *other.snapshot(players[3]);
    require(view.acting_player == 3 && view.own_tiles.hand.size() == 14, "configured dealer draws");
    require(view.deadline == Table::TimePoint{} + 250ms, "configured timeout applies");
    require(!other.snapshot(first.players[0]), "another table's player has no access");
    for (const auto options : {TableOptions{-1, 100, 0}, TableOptions{4, 100, 0}, TableOptions{0, 0, 0}}) {
        bool threw = false;
        try { Table invalid{RuleConfig{}, options}; }
        catch (const std::invalid_argument&) { threw = true; }
        require(threw, "invalid table options rejected");
    }
    RuleConfig config;
    config.game.playerCount = 3;
    bool threw = false;
    try { Table invalid{config}; }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw, "unsupported player count rejected");
}

void test_timeout_only_rounds_and_bounded_request_cache() {
    // 淘汰回执后仍拒绝旧请求；连续运行多局验证自动摸切、流局和累计计分。
    Fixture f;
    f.table.start(f.now);
    const auto initial_log = read_replay(f.table);
    const auto replay_path = f.table.replay().path();
    const auto earliest = f.command(0, PlayerActionType::Discard, null_tile);
    require(!f.table.submit(f.players[0], earliest, f.now).accepted, "invalid command rejected");
    for (int i = 0; i < 130; ++i) {
        const auto bad = f.command(0, PlayerActionType::Discard, null_tile);
        require(!f.table.submit(f.players[0], bad, f.now).accepted, "invalid retries stay rejected");
    }
    const auto old = f.table.submit(f.players[0], earliest, f.now);
    require(!old.accepted && !old.duplicate && old.error == "request id is too old", "evicted request cannot run again");
    require(read_replay(f.table) == initial_log, "rejected commands do not append replay events");
    std::array<std::int64_t, 4> expected_points{};
    for (int round = 0; round < 8; ++round) {
        int ticks = 0;
        while (f.view().table_stage == TableStage::Playing && ++ticks < 500) {
            f.now += 100ms;
            const auto update = f.table.tick(f.now);
            require(update.accepted && update.error.empty(), "timeout simulation advances");
        }
        require(f.view().table_stage == TableStage::RoundFinished, "timeout game terminates");
        require(!f.view().round_result->has_winner && f.view().point_result, "exhaustion settles");
        const auto settled = f.view();
        for (int seat = 0; seat < 4; ++seat) expected_points[seat] += settled.point_result->delta_result[seat];
        require(settled.total_points == expected_points, "each round contributes its points exactly once");
        const auto log = read_replay(f.table);
        require(f.table.replay().path() == replay_path, "next rounds reuse the same file");
        require(log.back()["type"] == "round_finished" && log.back()["round_id"] == round + 1
            && log.back()["total_points"] == expected_points, "timeout round settlement persisted");
        require(log.back()["round_result"]["win_tile"].is_null()
            && log.back()["round_result"]["chicken_indicator"].is_null(), "empty tiles use JSON null");
        if (round != 7) {
            for (const auto& player : f.players) f.table.ready(player);
            require(f.table.start(f.now).accepted, "subsequent timeout round starts");
        }
    }
    int starts = 0;
    int finishes = 0;
    std::uint64_t round_id = 0;
    std::uint64_t seq = 0;
    std::uint64_t table_seq = 0;
    for (const auto& entry : read_replay(f.table)) {
        if (entry["type"] == "table") continue;
        if (entry["type"] == "round_start") {
            ++starts;
            require(entry["round_id"] == ++round_id, "round IDs contiguous");
            seq = 0;
            continue;
        }
        require(entry["round_id"] == round_id && entry["seq_before"] == seq,
            "transition belongs to current round and follows previous transition");
        require(entry["seq_after"] == ++seq, "no duplicated or lost transitions");
        const auto next_table_seq = entry["table_seq"].get<std::uint64_t>();
        require(next_table_seq > table_seq, "table sequence increases across rounds");
        table_seq = next_table_seq;
        for (const auto& event : entry["events"]) require(event["seq"] == seq, "event order retained within batch");
        if (entry["type"] == "round_finished") ++finishes;
    }
    require(starts == 8 && finishes == 8, "all eight rounds retained in a single file");
}

void test_replay_metadata_and_storage_errors() {
    const std::string name = "name\"\\\n\t\r" + std::string(1, '\0') + "\xe9\xba\xbb\xe5\xb0\x86";
    RuleConfig rule;
    rule.game.allowMultiRon = false;
    rule.score.kan_point = 7;
    std::filesystem::path path;
    {
        Table table{rule, TableOptions{0, 100, 42, "replay/nested"}};
        path = table.replay().path();
        for (int seat = 0; seat < 4; ++seat) {
            const auto player = *table.join(PlayerInfo{name}).assigned_player;
            table.ready(player);
        }
        require(table.start(wall_with({}), Table::TimePoint{}).accepted, "fixed-wall game starts");
        const auto log = read_replay(table);
        require(log[1]["players"][0]["player_name"] == name, "JSON round-trips names and control characters");
        require(log[1]["config"]["rule"]["game"]["allowMultiRon"] == false
            && log[1]["config"]["rule"]["score"]["kan_point"] == 7, "custom rules captured");
        require(log[1]["wall_source"] == "provided", "fixed wall not confused with seeded shuffle");
        require(path.parent_path() == std::filesystem::path("replay/nested"), "configured replay directory used");
    }
    std::ifstream file(path, std::ios::binary);
    std::string line;
    int lines = 0;
    while (std::getline(file, line)) {
        require(nlohmann::json::accept(line), "interrupted round remains readable after table destruction");
        ++lines;
    }
    require(lines == 4, "interrupted round retains every flushed batch");
    bool threw = false;
    try { Table invalid{rule, TableOptions{0, 100, 42, path}}; }
    catch (const std::filesystem::filesystem_error&) { threw = true; }
    require(threw, "storage path that is a file fails explicitly");
    require(std::filesystem::file_size(path) > 0, "existing replay is not truncated on failure");
}

}

int main() {
    try {
        test_identity_and_lifecycle();
        test_start_and_visibility();
        test_request_validation_and_retries();
        test_parallel_claims_and_settlement();
        test_disconnect_and_timeout_windows();
        test_concealed_kan_visibility();
        test_failed_start_is_transactional();
        test_options_and_ids_across_tables();
        test_timeout_only_rounds_and_bounded_request_cache();
        test_replay_metadata_and_storage_errors();
    } catch (const std::exception& error) {
        std::cerr << "table_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "table_tests passed\n";
    return EXIT_SUCCESS;
}
