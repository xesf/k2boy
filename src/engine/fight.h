// fight.h — the combat tuning the game ships as text.
//
//   data/fight.ini   [Fight Params] "Key With Spaces = seconds": attack begin,
//                    recover, feint and window times, all multiples of 1/30 s
//   data/enemy.dat   value-first lines, the original C++ assignment kept as a
//                    comment ("10  distApproachEnemy = 5.0f; // charge within
//                    this distance"); the first number is the live value
//
// See docs/formats/text-and-tuning.md.
//
// C++11.

#ifndef K2_ENGINE_FIGHT_H
#define K2_ENGINE_FIGHT_H

#include <map>
#include <string>

namespace k2 {
namespace engine {

class fight_params {
public:
    // fight.ini text. Keys are kept as written; lookup ignores case.
    void parse_fight_ini(const char* text, size_t size);
    // enemy.dat text: the first number on each line, under the identifier
    // that follows it (distApproachEnemy, ...). Only the leading block is read.
    void parse_enemy_dat(const char* text, size_t size);

    // A fight.ini value, or `fallback`.
    double fight(const std::string& key, double fallback) const;
    // An enemy.dat value, or `fallback`.
    double enemy(const std::string& key, double fallback) const;

    size_t fight_count() const { return m_fight.size(); }
    size_t enemy_count() const { return m_enemy.size(); }

private:
    std::map<std::string, double> m_fight;
    std::map<std::string, double> m_enemy;
};

} // namespace engine
} // namespace k2

#endif // K2_ENGINE_FIGHT_H
