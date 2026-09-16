#include <gymj/core/room/round.hpp>

#include <algorithm>
#include <utility>

namespace gymj::room{

using namespace gymj::common;

namespace{

// 杠后补牌、自摸和紧接着的出牌共用此标记，直到本次出牌响应结束。
bool after_kan(DiscardDetail detail){
    return detail == DiscardDetail::AfterOpenKanDraw
        || detail == DiscardDetail::AfterSelfKanDraw
        || detail == DiscardDetail::AfterAddKanDraw;
}

void remove_tiles(std::vector<Tile>& hand, Tile tile, int count){
    // 调用前已通过合法动作校验，保证手牌中存在足够数量的目标牌。
    for(int i = 0; i < count; ++i){
        hand.erase(std::find(hand.begin(), hand.end(), tile));
    }
}

bool is_kan(PlayerActionType type){
    return type == PlayerActionType::SelfKan
        || type == PlayerActionType::AddKan
        || type == PlayerActionType::OpenKan;
}

}

Round::Round(RoundConfig config, std::array<PlayerInfo, 4> players, std::mt19937* rng)
    :config_(std::move(config)), players_(std::move(players)), rng_(rng),
     rule_engine_(config_.rule.score, config_.rule.game){
    // 保留种子的高低 32 位；未注入随机数引擎时由小局自行持有引擎。
    std::seed_seq seed{static_cast<std::uint32_t>(config_.seed),
                       static_cast<std::uint32_t>(config_.seed >> 32)};
    owned_rng_.seed(seed);
}

std::string Round::start_error() const{
    // 所有开局校验都在洗牌和发牌前完成，拒绝请求不会消耗随机数或牌山。
    if(state_.stage != RoundStage::NotActive){
        return "round already started";
    }
    if(config_.player_count != 4 || config_.rule.game.playerCount != 4){
        return "round requires exactly four players";
    }
    if(config_.dealer_seat < 0 || config_.dealer_seat >= 4){
        return "invalid dealer seat";
    }
    return {};
}

RoundTransition Round::transition_before() const{
    // 保存本次操作前的元信息，供调用方比较状态变化和生成协议消息。
    RoundTransition transition{};
    transition.seq_before = transition.seq_after = state_.seq;
    transition.stage_before = transition.stage_after = state_.stage;
    transition.actor_before = transition.actor_after = state_.acting_player;
    transition.round_ended = state_.stage == RoundStage::Ended;
    transition.round_result = result_;
    transition.point_result = point_result_;
    return transition;
}

RoundTransition Round::reject(const std::string& error) const{
    // 失败时仅返回错误和当前合法动作，不推进序号，也不追加牌谱事件。
    auto transition = transition_before();
    transition.error = error;
    transition.available_actions = available_actions();
    return transition;
}

RoundTransition Round::accept(RoundTransition transition){
    // 一次成功调用只增加一个序号；其中的多个事件按数组顺序归入同次推进。
    transition.accepted = true;
    transition.seq_after = ++state_.seq;
    state_.tiles_remaining = wall_.remaining();
    transition.stage_after = state_.stage;
    transition.actor_after = state_.acting_player;
    transition.round_ended = state_.stage == RoundStage::Ended;
    transition.round_result = result_;
    transition.point_result = point_result_;
    transition.available_actions = available_actions();
    for(auto& event : transition.events){
        event.seq = state_.seq;
    }
    return transition;
}

void Round::emit(RoundTransition& transition, RoundEventType type, int seat,
                 std::optional<Tile> tile, std::optional<PlayerAction> action, int from_seat){
    // 这里保存完整服务端事件，包含暗牌；网络层需要按座位过滤后再下发。
    RoundEvent event{};
    event.type = type;
    event.player_seat = seat;
    event.from_seat = from_seat;
    event.tile = tile;
    event.action = action;
    event.discard_detail = state_.discard_detail;
    transition.events.push_back(std::move(event));
}

RoundTransition Round::start(){
    // 牌山生成和洗牌由 Wall 负责，Round 只负责随后发牌和推进状态。
    const auto error = start_error();
    if(!error.empty()){
        return reject(error);
    }
    wall_.init(rng_ ? rng_ : &owned_rng_);
    return deal_initial_hands();
}

RoundTransition Round::start(const std::array<Tile, Wall::tile_count>& tiles){
    const auto error = start_error();
    if(!error.empty()){
        return reject(error);
    }
    std::array<int, tileKindCount> counts{};
    // 固定牌山用于测试和回放，必须仍是合法的 108 张牌。
    for(const auto tile : tiles){
        const int index = tile_index(tile);
        if(index < 0 || ++counts[index] > 4){
            return reject("invalid initial wall");
        }
    }

    wall_.init(tiles);
    return deal_initial_hands();
}

RoundTransition Round::deal_initial_hands(){
    auto transition = transition_before();
    // 从庄家开始，按座位逐张发牌，共发十三轮，庄家的第十四张由摸牌接口取得。
    for(int i = 0; i < 13; ++i){
        for(int offset = 0; offset < 4; ++offset){
            state_.states[(config_.dealer_seat + offset) % 4].hand.push_back(wall_.draw_tile());
        }
    }
    state_.stage = RoundStage::WaitingDraw;
    state_.acting_player = config_.dealer_seat;
    emit(transition, RoundEventType::RoundStarted, config_.dealer_seat);
    for(int seat = 0; seat < 4; ++seat){
        emit(transition, RoundEventType::InitialHands, seat);
        transition.events.back().tiles = state_.states[seat].hand;
    }
    return accept(std::move(transition));
}

std::array<std::vector<PlayerAction>, 4> Round::available_actions() const{
    // 规则引擎判断牌型；小局再过滤已响应座位和没有补牌可摸时的杠操作。
    auto actions = rule_engine_.get_available_actions(state_);
    for(int seat = 0; seat < 4; ++seat){
        if(state_.stage == RoundStage::WaitingClaim && state_.claims[seat]){
            actions[seat].clear();
        }
        if(wall_.empty()){
            auto& seat_actions = actions[seat];
            seat_actions.erase(std::remove_if(seat_actions.begin(), seat_actions.end(),
                [](const PlayerAction& action){ return is_kan(action.type); }), seat_actions.end());
        }
    }
    return actions;
}

RoundTransition Round::draw_for_current_player(){
    if(state_.stage != RoundStage::WaitingDraw){
        return reject("can not draw");
    }
    auto transition = transition_before();
    if(wall_.empty()){
        // 最后一张牌打出且响应结束后，下一次摸牌才触发荒牌结局。
        finish(transition, {}, WinType::NoWinner, WinDetail::NoWinner, null_tile);
    } else {
        const bool kan_draw = after_kan(state_.discard_detail);
        const auto tile = kan_draw ? wall_.draw_kan_tile() : wall_.draw_tile();
        auto& player = state_.states[state_.acting_player];
        player.hand.push_back(tile);
        // draw_buffer 只标识刚摸到的牌，牌本身已包含在 hand 中，不能重复计数。
        player.draw_buffer = tile;
        player.discard_buffer.reset();
        state_.stage = RoundStage::WaitingDiscard;
        if(!kan_draw){
            state_.discard_detail = DiscardDetail::SimpleDraw;
        }
        emit(transition, RoundEventType::PlayerDraw, state_.acting_player, tile);
    }
    return accept(std::move(transition));
}

RoundTransition Round::submit_action(int seat, PlayerAction action){
    // 只接受本座位当前被允许的动作和目标牌，校验完成前不修改任何状态。
    if(seat < 0 || seat >= 4){
        return reject("invalid player seat");
    }
    const auto actions = available_actions();
    const auto found = std::find_if(actions[seat].begin(), actions[seat].end(),
        [&](const PlayerAction& allowed){
            return allowed.type == action.type && allowed.action_tile == action.action_tile;
        });
    if(found == actions[seat].end()){
        return reject("illegal action");
    }

    auto transition = transition_before();
    if(state_.stage == RoundStage::WaitingClaim){
        // 先记录选择，等所有可响应玩家表态后统一裁决，避免到达顺序影响结果。
        state_.claims[seat] = action;
        emit(transition, RoundEventType::ClaimSubmitted, seat, action.action_tile, action);
        resolve_claims(transition);
        return accept(std::move(transition));
    }

    auto& player = state_.states[seat];
    const auto tile = action.action_tile;
    switch(action.type){
        case PlayerActionType::Discard:{
            remove_tiles(player.hand, tile, 1);
            player.draw_buffer.reset();
            player.discard_buffer = tile;
            player.river.push_back(tile);
            pending_dash_chicken_ = false;
            // 只记录本局首次打出的幺鸡或八筒，后续同牌不能改写原冲锋鸡归属。
            auto record_dash = [&](std::optional<DashChicken>& dash){
                if(!dash){
                    dash = DashChicken{tile, seat, -1, MeldType::Pon};
                    pending_dash_chicken_ = true;
                }
            };
            if(tile == Tile{TileType::Sou, 1}){
                record_dash(state_.one_sou);
            } else if(tile == Tile{TileType::Pin, 8}){
                record_dash(state_.eight_pin);
            }
            emit(transition, RoundEventType::PlayerDiscard, seat, tile, action);
            begin_claims(transition, action);
            break;
        }
        case PlayerActionType::SelfKan:
            // 暗杠直接落地，下一步从牌山尾部补牌。
            remove_tiles(player.hand, tile, 4);
            player.draw_buffer.reset();
            player.melds.push_back(Meld{MeldType::SelfKan, tile, static_cast<std::uint8_t>(seat)});
            state_.stage = RoundStage::WaitingDraw;
            state_.discard_detail = DiscardDetail::AfterSelfKanDraw;
            emit(transition, RoundEventType::MeldDeclared, seat, tile, action, seat);
            break;
        case PlayerActionType::AddKan:
            // 加杠先开启抢杠窗口，此时仍保留原碰和手中的第四张牌。
            emit(transition, RoundEventType::AddKanProposed, seat, tile, action);
            begin_claims(transition, action);
            break;
        case PlayerActionType::Tsumo:
            finish(transition, {seat}, WinType::Tsumo,
                   after_kan(state_.discard_detail) ? WinDetail::TsumoFromKan : WinDetail::Simple, tile);
            break;
        default:
            break;
    }
    return accept(std::move(transition));
}

void Round::begin_claims(RoundTransition& transition, PlayerAction action){
    // acting_player 在响应阶段指向出牌者或加杠者，无合法响应的座位自动过。
    state_.stage = RoundStage::WaitingClaim;
    state_.pending_action = action;
    state_.claims = {};
    const auto actions = available_actions();
    for(int seat = 0; seat < 4; ++seat){
        if(actions[seat].empty()){
            state_.claims[seat] = PlayerAction{PlayerActionType::Pass, action.action_tile};
        }
    }
    resolve_claims(transition);
}

void Round::complete_add_kan(RoundTransition& transition){
    // 无人抢杠后才将碰升级为加杠，并保留原碰的供牌座位。
    const auto action = *state_.pending_action;
    auto& player = state_.states[state_.acting_player];
    const auto meld = std::find_if(player.melds.begin(), player.melds.end(),
        [&](const Meld& value){ return value.type == MeldType::Pon && value.tile == action.action_tile; });
    meld->type = MeldType::AddKan;
    remove_tiles(player.hand, action.action_tile, 1);
    player.draw_buffer.reset();
    state_.stage = RoundStage::WaitingDraw;
    state_.discard_detail = DiscardDetail::AfterAddKanDraw;
    emit(transition, RoundEventType::MeldDeclared, state_.acting_player, action.action_tile,
         action, meld->from_seat);
}

void Round::resolve_claims(RoundTransition& transition){
    // 尚有玩家未回复时保持等待；全部回复后交给规则引擎处理胡牌优先和多响。
    std::array<PlayerAction, 4> claims{};
    for(int seat = 0; seat < 4; ++seat){
        if(!state_.claims[seat]){
            return;
        }
        claims[seat] = *state_.claims[seat];
    }
    const int source = state_.acting_player;
    const auto pending = *state_.pending_action;
    const auto tile = pending.action_tile;
    const auto resolved = rule_engine_.resolve_claims(claims, source);

    if(!resolved.empty() && resolved.front().second.type == PlayerActionType::Ron){
        std::vector<int> winners;
        for(const auto& claim : resolved){
            winners.push_back(claim.first);
        }
        WinDetail detail = after_kan(state_.discard_detail) ? WinDetail::RonKanDiscard : WinDetail::Simple;
        if(pending.type == PlayerActionType::AddKan){
            detail = WinDetail::RonAddKan;
            // 抢杠仅取走第四张牌，原有碰不回滚也不升级。
            remove_tiles(state_.states[source].hand, tile, 1);
            state_.states[source].draw_buffer.reset();
        } else {
            state_.states[source].river.pop_back();
        }
        finish(transition, winners, WinType::Ron, detail, tile);
        return;
    }

    if(resolved.empty()){
        if(pending.type == PlayerActionType::AddKan){
            complete_add_kan(transition);
        } else {
            state_.acting_player = (source + 1) % 4;
            state_.stage = RoundStage::WaitingDraw;
            state_.discard_detail = DiscardDetail::None;
        }
    } else {
        const int seat = resolved.front().first;
        const auto action = resolved.front().second;
        const bool kan = action.type == PlayerActionType::OpenKan;
        const auto meld_type = kan ? MeldType::OpenKan : MeldType::Pon;
        auto& player = state_.states[seat];
        remove_tiles(player.hand, tile, kan ? 3 : 2);
        player.melds.push_back(Meld{meld_type, tile, static_cast<std::uint8_t>(source)});
        // 被碰杠的牌转入副露，必须从牌河移除，保证实体牌数量守恒。
        state_.states[source].river.pop_back();
        if(pending_dash_chicken_){
            auto& dash = tile == Tile{TileType::Sou, 1} ? state_.one_sou : state_.eight_pin;
            dash->claimed_by = seat;
            dash->claim_type = meld_type;
        }
        state_.acting_player = seat;
        state_.stage = kan ? RoundStage::WaitingDraw : RoundStage::WaitingDiscard;
        state_.discard_detail = kan ? DiscardDetail::AfterOpenKanDraw : DiscardDetail::AfterPon;
        emit(transition, RoundEventType::MeldDeclared, seat, tile, action, source);
    }
    state_.states[source].discard_buffer.reset();
    // 响应窗口已结束，清理临时选择，下一次出牌重新收集响应。
    state_.pending_action.reset();
    state_.claims = {};
    pending_dash_chicken_ = false;
}

void Round::finish(RoundTransition& transition, const std::vector<int>& winners,
                   WinType type, WinDetail detail, Tile tile){
    // 固化结局快照；多响共用同一张实体胡牌，但记录每个赢家。
    RoundResult result{};
    result.has_winner = !winners.empty();
    result.winner_seats = winners;
    result.winner_seat = winners.empty() ? -1 : winners.front();
    result.discarder_seat = type == WinType::Ron ? state_.acting_player : -1;
    result.win_tile = tile;
    result.win_type = type;
    result.detail = detail;
    for(auto& player : state_.states){
        player.discard_buffer.reset();
    }
    result.states = state_.states;
    // 计分引擎要求胡牌单独保存；自摸快照移出该牌，使手牌保持可判听的张数。
    if(type == WinType::Tsumo){
        auto& winner = result.states[result.winner_seat];
        remove_tiles(winner.hand, tile, 1);
        winner.draw_buffer.reset();
    }
    result.one_sou = state_.one_sou;
    result.eight_pin = state_.eight_pin;
    result_ = std::move(result);
    for(const int seat : winners){
        emit(transition, RoundEventType::PlayerWin, seat, tile,
             PlayerAction{type == WinType::Tsumo ? PlayerActionType::Tsumo : PlayerActionType::Ron, tile},
             result_->discarder_seat);
    }
    state_.stage = RoundStage::Ended;
    state_.acting_player = -1;
    state_.pending_action.reset();
    state_.claims = {};
    state_.discard_detail = DiscardDetail::None;
    pending_dash_chicken_ = false;
    emit(transition, RoundEventType::RoundEnded);
    const auto ended_index = transition.events.size() - 1;

    // 有剩余牌时从正常摸牌端取一张作指示牌；同花色点数加一，九回到一。
    // 指示牌独立记录，不属于任何玩家；空牌山不翻鸡，round_chicken 保持空牌。
    if(!wall_.empty()){
        // 使用剩余牌山的实际堆顶，而不是初始牌山下标；杠补牌后也遵守同一规则。
        const auto indicator = wall_.draw_tile();
        result_->chicken_indicator = indicator;
        result_->round_chicken = Tile{indicator.type, static_cast<std::uint8_t>(indicator.rank % 9 + 1)};
        emit(transition, RoundEventType::ChickenRevealed, -1, indicator);
    }
}

RoundTransition Round::handle_timeout(int seat){
    // 计时由牌桌负责；小局只执行超时策略：响应时过牌，摸牌后摸切，碰后切末张。
    if(seat < 0 || seat >= 4){
        return reject("invalid player seat");
    }
    const auto actions = available_actions();
    if(state_.stage == RoundStage::WaitingClaim){
        const auto pass = std::find_if(actions[seat].begin(), actions[seat].end(),
            [](const PlayerAction& action){ return action.type == PlayerActionType::Pass; });
        if(pass != actions[seat].end()){
            return submit_action(seat, *pass);
        }
    } else if(state_.stage == RoundStage::WaitingDiscard && seat == state_.acting_player){
        const auto& player = state_.states[seat];
        return submit_action(seat, PlayerAction{PlayerActionType::Discard,
            player.draw_buffer.value_or(player.hand.back())});
    }
    return reject("no action to time out");
}

RoundTransition Round::settle(){
    // 使用结束时已确定的翻鸡计算一次小分，重复调用既不再翻牌也不重复收费。
    if(state_.stage != RoundStage::Ended){
        return reject("round has not ended");
    }
    if(point_result_){
        return reject("round already settled");
    }
    auto transition = transition_before();
    // 翻鸡已在结束动作中完成，此处只读结局，避免结算重试再次消耗牌山。
    const auto round_chicken = result_->round_chicken;
    point_result_ = rule_engine_.calculate_points(*result_, round_chicken);
    emit(transition, RoundEventType::PointsCalculated, -1,
         round_chicken == null_tile ? std::nullopt : std::optional<Tile>{round_chicken});
    return accept(std::move(transition));
}

}
