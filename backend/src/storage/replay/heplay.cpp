#include <gymj/storage/replay/replay.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace gymj::common {

NLOHMANN_JSON_SERIALIZE_ENUM(RoundEventType, {
    {RoundEventType::RoundStarted, "RoundStarted"},
    {RoundEventType::InitialHands, "InitialHands"},
    {RoundEventType::PlayerDraw, "PlayerDraw"},
    {RoundEventType::PlayerDiscard, "PlayerDiscard"},
    {RoundEventType::ClaimSubmitted, "ClaimSubmitted"},
    {RoundEventType::MeldDeclared, "MeldDeclared"},
    {RoundEventType::AddKanProposed, "AddKanProposed"},
    {RoundEventType::PlayerWin, "PlayerWin"},
    {RoundEventType::RoundEnded, "RoundEnded"},
    {RoundEventType::PointsCalculated, "PointsCalculated"},
    {RoundEventType::ChickenRevealed, "ChickenRevealed"}
})
NLOHMANN_JSON_SERIALIZE_ENUM(PlayerActionType, {
    {PlayerActionType::None, "None"}, {PlayerActionType::Tsumo, "Tsumo"},
    {PlayerActionType::Ron, "Ron"}, {PlayerActionType::Pon, "Pon"},
    {PlayerActionType::SelfKan, "SelfKan"}, {PlayerActionType::AddKan, "AddKan"},
    {PlayerActionType::OpenKan, "OpenKan"}, {PlayerActionType::Pass, "Pass"},
    {PlayerActionType::Discard, "Discard"}
})
NLOHMANN_JSON_SERIALIZE_ENUM(DiscardDetail, {
    {DiscardDetail::None, "None"}, {DiscardDetail::SimpleDraw, "SimpleDraw"},
    {DiscardDetail::AfterPon, "AfterPon"}, {DiscardDetail::AfterOpenKanDraw, "AfterOpenKanDraw"},
    {DiscardDetail::AfterSelfKanDraw, "AfterSelfKanDraw"},
    {DiscardDetail::AfterAddKanDraw, "AfterAddKanDraw"}
})
NLOHMANN_JSON_SERIALIZE_ENUM(RoundStage, {
    {RoundStage::NotActive, "NotActive"}, {RoundStage::WaitingDraw, "WaitingDraw"},
    {RoundStage::WaitingDiscard, "WaitingDiscard"}, {RoundStage::WaitingClaim, "WaitingClaim"},
    {RoundStage::Ended, "Ended"}
})
NLOHMANN_JSON_SERIALIZE_ENUM(MeldType, {
    {MeldType::Pon, "Pon"}, {MeldType::OpenKan, "OpenKan"},
    {MeldType::SelfKan, "SelfKan"}, {MeldType::AddKan, "AddKan"}
})
NLOHMANN_JSON_SERIALIZE_ENUM(WinType, {
    {WinType::NoWinner, "NoWinner"}, {WinType::Ron, "Ron"}, {WinType::Tsumo, "Tsumo"}
})
NLOHMANN_JSON_SERIALIZE_ENUM(WinDetail, {
    {WinDetail::NoWinner, "NoWinner"}, {WinDetail::Simple, "Simple"},
    {WinDetail::TsumoFromKan, "TsumoFromKan"}, {WinDetail::RonKanDiscard, "RonKanDiscard"},
    {WinDetail::RonAddKan, "RonAddKan"}
})

using nlohmann::json;

void to_json(json& out, const Tile& tile) {
    out = tile == null_tile ? json(nullptr) : json(tile_to_string(tile));
}

template<class T>
json optional_json(const std::optional<T>& value) {
    return value ? json(*value) : json(nullptr);
}

void to_json(json& out, const PlayerAction& action) {
    out = {{"type", action.type}, {"action_tile", action.action_tile}};
}

void to_json(json& out, const RoundEvent& event) {
    out = {{"type", event.type}, {"seq", event.seq}, {"player_seat", event.player_seat},
        {"from_seat", event.from_seat}, {"tile", optional_json(event.tile)},
        {"tiles", event.tiles}, {"action", optional_json(event.action)},
        {"discard_detail", event.discard_detail}};
}

void to_json(json& out, const Meld& meld) {
    out = {{"type", meld.type}, {"tile", meld.tile}, {"from_seat", meld.from_seat}};
}

void to_json(json& out, const PlayerTileState& state) {
    out = {{"hand", state.hand}, {"river", state.river}, {"melds", state.melds},
        {"draw_buffer", optional_json(state.draw_buffer)},
        {"discard_buffer", optional_json(state.discard_buffer)}};
}

void to_json(json& out, const DashChicken& chicken) {
    out = {{"tile", chicken.tile}, {"discarded_by", chicken.discarded_by},
        {"claimed_by", chicken.claimed_by}, {"claim_type", chicken.claim_type}};
}

void to_json(json& out, const RoundResult& result) {
    out = {{"has_winner", result.has_winner}, {"winner_seat", result.winner_seat},
        {"winner_seats", result.winner_seats}, {"discarder_seat", result.discarder_seat},
        {"win_tile", result.win_tile}, {"win_type", result.win_type}, {"detail", result.detail},
        {"states", result.states}, {"one_sou", optional_json(result.one_sou)},
        {"eight_pin", optional_json(result.eight_pin)}, {"round_chicken", result.round_chicken},
        {"chicken_indicator", optional_json(result.chicken_indicator)}};
}

void to_json(json& out, const PointDetail& detail) {
    out = {{"total_point", detail.total_point}, {"point_from_chicken", detail.point_from_chicken},
        {"point_from_kan", detail.point_from_kan}, {"point_from_agari", detail.point_from_agari},
        {"point_from_tenpai", detail.point_from_tenpai}};
}

void to_json(json& out, const PointResult& result) {
    out = {{"point_to_others", result.point_to_others}, {"delta_result", result.delta_result},
        {"detail", result.detail}};
}

void to_json(json& out, const RuleConfig& rule) {
    out = {{"game", {{"allowMultiRon", rule.game.allowMultiRon}, {"playerCount", rule.game.playerCount}}},
        {"score", {{"tsumo_point", rule.score.tsumo_point},
            {"half_same_color_point", rule.score.half_same_color_point},
            {"same_color_point", rule.score.same_color_point},
            {"dash_chicken_point", rule.score.dash_chicken_point},
            {"hand_chicken_point", rule.score.hand_chicken_point},
            {"kan_point", rule.score.kan_point},
            {"allow_no_chicken_no_kan", rule.score.allow_no_chicken_no_kan}}}};
}

}

namespace gymj::storage {
using nlohmann::json;

Replay::Replay(const std::filesystem::path& directory) {
    if (directory.empty()) throw std::invalid_argument("empty replay directory");
    std::filesystem::create_directories(directory);
    static std::atomic<std::uint64_t> next{0};
    std::random_device random;
    do {
        std::ostringstream name;
        name << "table-" << std::chrono::system_clock::now().time_since_epoch().count()
             << '-' << next.fetch_add(1, std::memory_order_relaxed) << '-' << std::hex
             << random() << '-' << random() << ".jsonl";
        path_ = directory / name.str();
    } while (std::filesystem::exists(path_));
    // Append mode never truncates an existing export.
    stream_.open(path_, std::ios::binary | std::ios::app);
    if (!stream_) throw std::runtime_error("cannot open replay: " + path_.string());
    if (!write_line(json{{"type", "table"}, {"version", 1},
                        {"table_id", path_.stem().string()}}.dump())) {
        throw std::runtime_error(error_);
    }
}

bool Replay::write_line(std::string_view line) {
    if (!error_.empty()) return false;
    stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
    stream_.put('\n');
    stream_.flush();
    if (!stream_) {
        // A partial record must not be followed by more records or reported as saved.
        error_ = "cannot write replay: " + path_.string();
        return false;
    }
    return true;
}

bool Replay::start_round(std::uint64_t round_id, const common::RoundConfig& config,
                         const std::array<common::PlayerInfo, 4>& players, bool fixed_wall) {
    if (!error_.empty()) return false;
    if (!settled_ || round_id != round_id_ + 1) throw std::logic_error("invalid replay round start");
    auto seats = json::array();
    for (int seat = 0; seat < 4; ++seat) {
        seats.push_back({{"seat", seat}, {"player_name", players[seat].player_name}});
    }
    const json entry = {{"type", "round_start"}, {"round_id", round_id}, {"players", seats},
        {"config", {{"dealer_seat", config.dealer_seat}, {"player_count", config.player_count},
            {"action_timeout_ms", config.action_timeout_ms}, {"seed", config.seed}, {"rule", config.rule}}},
        {"wall_source", fixed_wall ? "provided" : "shuffled"}};
    // Preserve valid UTF-8 names, replacing invalid bytes rather than losing the game log.
    if (!write_line(entry.dump(-1, ' ', false, json::error_handler_t::replace))) return false;
    round_id_ = round_id;
    last_seq_ = 0;
    settled_ = false;
    return true;
}

void Replay::record(std::uint64_t round_id, std::uint64_t table_seq,
                    const common::RoundTransition& transition,
                    const std::array<std::int64_t, 4>& total_points) {
    if (!transition.accepted || !error_.empty()) return;
    if (settled_ || round_id != round_id_ || transition.seq_before != last_seq_
        || transition.seq_after != last_seq_ + 1) {
        throw std::logic_error("out-of-order replay transition");
    }
    const bool settled = std::any_of(transition.events.begin(), transition.events.end(),
        [](const common::RoundEvent& event) { return event.type == common::RoundEventType::PointsCalculated; });
    const json entry = {{"type", settled ? "round_finished" : "round_events"},
        {"round_id", round_id}, {"table_seq", table_seq},
        {"seq_before", transition.seq_before}, {"seq_after", transition.seq_after},
        {"stage_before", transition.stage_before}, {"stage_after", transition.stage_after},
        {"actor_before", transition.actor_before}, {"actor_after", transition.actor_after},
        {"events", transition.events}, {"round_result", common::optional_json(transition.round_result)},
        {"point_result", common::optional_json(transition.point_result)}, {"total_points", total_points}};
    if (write_line(entry.dump())) {
        last_seq_ = transition.seq_after;
        settled_ = settled;
    }
}

}
