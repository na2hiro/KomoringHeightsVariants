#include "../../extra/all.h"
#include "variant_usi.hpp"

void user_test(Position&, std::istringstream&) {}
void USI::extra_option(USI::OptionsMap&) {}
void Search::init() {}
void Search::clear() {}
void MainThread::search() {}
void Thread::search() {}

namespace komori::variant {
Rule ActiveRule() { return Rule::standard; }
bool RuleChangePending() { return false; }
bool SetPosition(Position&, StateInfo&, Thread*, const std::string&) { return false; }
Move ParseMove(const Position&, const std::string&, Bitboard*) { return MOVE_NONE; }
std::vector<Move> LegalMoves(const Position&) { return {}; }
}  // namespace komori::variant
