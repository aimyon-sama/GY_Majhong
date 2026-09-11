#ifndef GYMJ_CORE_ROUND_HPP
#define GYMJ_CORE_ROUND_HPP

#include <gymj/common/schema/round_state.hpp>
#include <gymj/core/room/wall.hpp>
#include <gymj/core/rules/rule_engine.hpp>

namespace gymj::room{

// Call serially from the owning table. No network, clock or file I/O is performed.
class Round{
public:
    explicit Round(common::RoundConfig config,
                   std::array<common::PlayerInfo, 4> players = {},
                   std::mt19937* rng = nullptr);

    common::RoundTransition start();
    // Physical stack order for deterministic fixtures.
    common::RoundTransition start(const std::array<common::Tile, Wall::tile_count>& tiles);
    common::RoundTransition draw_for_current_player();
    common::RoundTransition submit_action(int seat, common::PlayerAction action);
    // The table decides when a deadline expires. This applies pass or automatic discard.
    common::RoundTransition handle_timeout(int seat);
    // Call once after Ended; finish() has already revealed the round chicken.
    common::RoundTransition settle();

    const common::RoundState& state() const noexcept { return state_; }
    const common::RoundConfig& config() const noexcept { return config_; }
    const std::array<common::PlayerInfo, 4>& players() const noexcept { return players_; }
    const std::optional<common::RoundResult>& result() const noexcept { return result_; }
    const std::optional<common::PointResult>& point_result() const noexcept { return point_result_; }
    std::array<std::vector<common::PlayerAction>, 4> available_actions() const;

private:
    std::string start_error() const;
    common::RoundTransition deal_initial_hands();
    common::RoundTransition transition_before() const;
    common::RoundTransition reject(const std::string& error) const;
    common::RoundTransition accept(common::RoundTransition transition);
    void emit(common::RoundTransition& transition, common::RoundEventType type,
              int seat = -1, std::optional<common::Tile> tile = std::nullopt,
              std::optional<common::PlayerAction> action = std::nullopt, int from_seat = -1);
    void begin_claims(common::RoundTransition& transition, common::PlayerAction action);
    void resolve_claims(common::RoundTransition& transition);
    void complete_add_kan(common::RoundTransition& transition);
    void finish(common::RoundTransition& transition, const std::vector<int>& winners,
                common::WinType type, common::WinDetail detail, common::Tile tile);

    common::RoundConfig config_;
    std::array<common::PlayerInfo, 4> players_;
    // An injected generator must outlive start(); the default generator is owned.
    std::mt19937* rng_;
    std::mt19937 owned_rng_;
    rule::RuleEngine rule_engine_;
    Wall wall_;
    common::RoundState state_;
    bool pending_dash_chicken_ = false;
    std::optional<common::RoundResult> result_;
    std::optional<common::PointResult> point_result_;
};
}

#endif
