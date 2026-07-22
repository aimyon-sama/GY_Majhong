#ifndef GYMJ_COMMON_RULE_CONFIG_HPP
#define GYMJ_COMMON_RULE_CONFIG_HPP

namespace gymj::common{

struct ScoreRuleConfig{
    int tsumoPoint = 3;
    int halfSameColorPoint = 8;
    int sameColorPoint = 17;
    int dashChickenPoint = 3;
    int handChickenPoint = 1;
    int kanPoint = 5;
};

struct GameRuleConfig{
    bool allowMultiRon = true;
    int playerCount = 4;
};

struct RuleConfig{
    GameRuleConfig game;
    ScoreRuleConfig score;
};

}

#endif