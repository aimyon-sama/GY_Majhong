#ifndef GYMJ_STORAGE_REPLAY_HPP
#define GYMJ_STORAGE_REPLAY_HPP

#include <filesystem>
#include <fstream>
#include <string_view>

#include <gymj/common/schema/round_state.hpp>

namespace gymj::storage {

// One append-only JSONL file per table. Call serially with authoritative events.
class Replay {
public:
    explicit Replay(const std::filesystem::path& directory = "replay");
    Replay(const Replay&) = delete;
    Replay& operator=(const Replay&) = delete;

    const std::filesystem::path& path() const noexcept { return path_; }
    const std::string& error() const noexcept { return error_; }

    bool start_round(std::uint64_t round_id, const common::RoundConfig& config,
                     const std::array<common::PlayerInfo, 4>& players, bool fixed_wall = false);
    void record(std::uint64_t round_id, std::uint64_t table_seq,
                const common::RoundTransition& transition,
                const std::array<std::int64_t, 4>& total_points);

private:
    bool write_line(std::string_view line);

    std::filesystem::path path_;
    std::ofstream stream_;
    std::string error_;
    std::uint64_t round_id_ = 0;
    std::uint64_t last_seq_ = 0;
    bool settled_ = true;
};

}

#endif
