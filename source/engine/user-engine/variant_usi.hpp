#ifndef KOMORI_VARIANT_USI_HPP_
#define KOMORI_VARIANT_USI_HPP_

#include "variant_rules.hpp"

namespace komori::variant {

Rule ActiveRule();
bool RuleChangePending();
bool SetPosition(Position& pos, StateInfo& state, Thread* thread, const std::string& sfen);
Move ParseMove(const Position& pos, const std::string& token, Bitboard* checkers = nullptr);
std::vector<Move> LegalMoves(const Position& pos);

}  // namespace komori::variant

#endif  // KOMORI_VARIANT_USI_HPP_
