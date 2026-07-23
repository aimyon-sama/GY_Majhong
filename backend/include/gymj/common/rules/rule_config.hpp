#ifndef GYMJ_COMMON_RULE_CONFIG_HPP
#define GYMJ_COMMON_RULE_CONFIG_HPP

namespace gymj::common{

struct PointRuleConfig{
    int tsumo_point = 3;
    int half_same_color_point = 8;
    int same_color_point = 17;
    int dash_chicken_point = 3;
    int hand_chicken_point = 1;
    int kan_point = 5;
};

struct GameRuleConfig{
    bool allowMultiRon = true;
    int playerCount = 4;
};

struct RuleConfig{
    GameRuleConfig game;
    PointRuleConfig score;
};

}

#endif