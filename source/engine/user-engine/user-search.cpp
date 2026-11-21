#include "komoring_heights.hpp"
#include "path_keys.hpp"
#include "thread_initialization.hpp"
#include "typedefs.hpp"
#include "variant_usi.hpp"

#include <charconv>

#if defined(USER_ENGINE)

namespace {
using komori::variant::Rule;

class Searcher {
 public:
  virtual ~Searcher() = default;
  virtual void Init(const komori::EngineOption&, std::uint32_t) = 0;
  virtual void NewSearch(const Position&, bool) = 0;
  virtual komori::NodeState Search(const Position&, bool) = 0;
  virtual const std::vector<Move>& BestMoves() const = 0;
};

template <Rule R>
class RuleSearcher final : public Searcher {
 public:
  void Init(const komori::EngineOption& option, std::uint32_t threads) override { searcher_.Init(option, threads); }
  void NewSearch(const Position& pos, bool attacker) override { searcher_.NewSearch(pos, attacker); }
  komori::NodeState Search(const Position& pos, bool attacker) override { return searcher_.Search(pos, attacker); }
  const std::vector<Move>& BestMoves() const override { return searcher_.BestMoves(); }

 private:
  komori::KomoringHeightsFor<R> searcher_;
};

std::unique_ptr<Searcher> g_searcher;
Rule g_requested_rule = komori::variant::kDefaultRule;
Rule g_active_rule = komori::variant::kDefaultRule;
komori::EngineOption g_option;
std::atomic_bool g_path_key_init_flag;

komori::NodeState g_search_result = komori::NodeState::kUnknown;

/// 局面が OR node っぽいかどうかを調べる。困ったら OR node として処理する。
bool IsPosOrNode(const Position& root_pos) {
  const Color us = root_pos.side_to_move();
  const Color them = ~us;

  if (root_pos.king_square(us) == SQ_NB) {
    return true;
  } else if (root_pos.king_square(them) == SQ_NB) {
    return false;
  }

  if (root_pos.in_check() && g_option.root_is_and_node_if_checked) {
    return false;
  }
  return true;
}

enum class LoseKind {
  kTimeout,
  kNoMate,
  kMate,
};

void PrintResult(bool is_mate_search, LoseKind kind, const std::string& pv_moves = "resign") {
  if (is_mate_search) {
    switch (kind) {
      case LoseKind::kTimeout:
        sync_cout << "checkmate timeout" << sync_endl;
        break;
      case LoseKind::kNoMate:
        sync_cout << "checkmate nomate" << sync_endl;
        break;
      default:
        sync_cout << "checkmate " << pv_moves << sync_endl;
    }
  } else {
    // `KomoringHeights::Search()` 内で出力しているはずなので、ここでは何もする必要がない。
  }
}

bool IsValidVariantSfen(const std::string& sfen) {
  std::istringstream input{sfen};
  std::string board, turn, hands, ply, extra;
  if (!(input >> board >> turn >> hands >> ply) || (input >> extra) || (turn != "b" && turn != "w")) {
    return false;
  }
  unsigned move_number = 0;
  const auto parsed = std::from_chars(ply.data(), ply.data() + ply.size(), move_number);
  if (parsed.ec != std::errc{} || parsed.ptr != ply.data() + ply.size() || move_number == 0 ||
      move_number > INT_MAX - MAX_PLY - komori::kDepthMax) {
    return false;
  }

  constexpr int inventory[] = {0, 18, 4, 4, 4, 2, 2, 4};
  int pieces[PIECE_HAND_NB]{};
  int kings[COLOR_NB]{};
  int files = 0;
  int ranks = 1;
  bool promoted = false;
  for (const char ch : board) {
    if (ch == '/' && !promoted && files == FILE_NB && ranks < RANK_NB) {
      files = 0;
      ++ranks;
    } else if (ch >= '1' && ch <= '9' && !promoted) {
      files += ch - '0';
    } else if (ch == '+' && !promoted) {
      promoted = true;
    } else {
      const auto index = PieceToCharBW.find(ch);
      if (index == std::string::npos) {
        return false;
      }
      const auto pc = static_cast<Piece>(index);
      const auto pt = type_of(pc);
      if (pt < PAWN || pt > KING || (promoted && pt > ROOK)) {
        return false;
      }
      if (pt == KING) {
        if (++kings[color_of(pc)] > 1) {
          return false;
        }
      } else if (++pieces[pt] > inventory[pt]) {
        return false;
      }
      ++files;
      promoted = false;
    }
    if (files > FILE_NB) {
      return false;
    }
  }
  if (files != FILE_NB || ranks != RANK_NB || promoted || kings[BLACK] + kings[WHITE] == 0) {
    return false;
  }
  if (hands == "-") {
    return true;
  }
  int count = 0;
  for (const char ch : hands) {
    if (ch >= '0' && ch <= '9') {
      count = 10 * count + ch - '0';
      if (count == 0 || count > 18) {
        return false;
      }
      continue;
    }
    const auto index = PieceToCharBW.find(ch);
    if (index == std::string::npos) {
      return false;
    }
    const auto pt = type_of(static_cast<Piece>(index));
    if (pt < PAWN || pt >= PIECE_HAND_NB) {
      return false;
    }
    pieces[pt] += std::max(1, count);
    if (pieces[pt] > inventory[pt]) {
      return false;
    }
    count = 0;
  }
  return count == 0;
}
}  // namespace

namespace komori::variant {

Rule ActiveRule() {
  return g_active_rule;
}
bool RuleChangePending() {
  return g_active_rule != g_requested_rule;
}

bool SetPosition(Position& pos, StateInfo& state, Thread* thread, const std::string& sfen) {
  if (g_active_rule != Rule::standard && !IsValidVariantSfen(sfen)) {
    return false;
  }
  pos.set(sfen, &state, thread, g_active_rule != Rule::standard);
  return Dispatch(g_active_rule, [&](auto rule) {
    if constexpr (rule() != Rule::standard) {
      const Board board{pos};
      if (Checkers<rule()>(board, ~pos.side_to_move())) {
        return false;
      }
      state.checkersBB = Checkers<rule()>(board, pos.side_to_move());
    }
    return true;
  });
}

Move ParseMove(const Position& pos, const std::string& token, Bitboard* checkers) {
  if (token.size() != 4 && (token.size() != 5 || token[4] != '+' || token[1] == '*')) {
    return MOVE_NONE;
  }
  const auto move = pos.to_move(USI::to_move16(token));
  return Dispatch(g_active_rule, [&](auto rule) {
    if constexpr (rule() != Rule::standard) {
      if (const auto legal = TryMove<rule()>(pos, move)) {
        if (checkers) {
          *checkers = *legal;
        }
        return move;
      }
    }
    return MOVE_NONE;
  });
}

std::vector<Move> LegalMoves(const Position& pos) {
  return Dispatch(g_active_rule, [&](auto rule) {
    std::vector<Move> moves;
    if constexpr (rule() != Rule::standard) {
      Generate<rule()>(Board{pos}, pos.side_to_move(), [&](Move move, Bitboard) {
        moves.push_back(move);
        return false;
      });
    }
    return moves;
  });
}

}  // namespace komori::variant

// USI拡張コマンド"user"が送られてくるとこの関数が呼び出される。実験に使ってください。
void user_test(Position& pos, std::istringstream& is) {}

// USIに追加オプションを設定したいときは、この関数を定義すること。
// USI::init()のなかからコールバックされる。
void USI::extra_option(USI::OptionsMap& o) {
  komori::EngineOption::Init(o);
#if !defined(KOMORI_FIXED_RULE)
  std::vector<std::string> names;
  for (const auto& info : komori::variant::kRules) {
    names.emplace_back(info.name);
  }
  o["RuleSet"] << USI::Option(names, "standard", [](const USI::Option& option) {
    const auto name = static_cast<std::string>(option);
    for (const auto& info : komori::variant::kRules) {
      if (info.name == name) {
        g_requested_rule = info.rule;
        return;
      }
    }
    sync_cout << "info string Unknown RuleSet " << name << sync_endl;
  });
#endif
}

// 起動時に呼び出される。時間のかからない探索関係の初期化処理はここに書くこと。
void Search::init() {}

// isreadyコマンドの応答中に呼び出される。時間のかかる処理はここに書くこと。
void Search::clear() {
  Threads.main()->wait_for_search_finished();
  Threads.clear();

  if (!g_path_key_init_flag) {
    g_path_key_init_flag = true;
    komori::PathKeyInit();
  }
  g_option.Reload(Options);

#if defined(USE_DEEP_DFPN)
  auto d = g_option.deep_dfpn_d_;
  auto e = g_option.deep_dfpn_e_;
  komori::DeepDfpnInit(d, e);
#endif  // defined(USE_DEEP_DFPN)

  if (!g_searcher || komori::variant::RuleChangePending()) {
    g_active_rule = g_requested_rule;
    g_searcher = komori::variant::Dispatch(
        g_active_rule, [](auto rule) -> std::unique_ptr<Searcher> { return std::make_unique<RuleSearcher<rule()>>(); });
  }
  g_searcher->Init(g_option, Threads.size());
}

// 探索開始時に呼び出される。
// この関数内で初期化を終わらせ、slaveスレッドを起動してThread::search()を呼び出す。
// そのあとslaveスレッドを終了させ、ベストな指し手を返すこと。
void MainThread::search() {
  // `go mate` で探索開始したときは true、`go` で探索開始したときは false
  const bool is_mate_search = Search::Limits.mate != 0;
  const bool is_root_or_node = IsPosOrNode(rootPos);

  g_searcher->NewSearch(rootPos, is_root_or_node);
  Threads.start_searching();
  Thread::search();
  Threads.stop = true;
  Threads.wait_for_search_finished();

  Move best_move = MOVE_NONE;
  if (g_search_result == komori::NodeState::kProven) {
    auto best_moves = g_searcher->BestMoves();
    PrintResult(is_mate_search, LoseKind::kMate, komori::ToString(best_moves));

    if (!best_moves.empty()) {
      best_move = best_moves[0];
    }
  } else {
    if (g_search_result == komori::NodeState::kDisproven || g_search_result == komori::NodeState::kRepetition) {
      PrintResult(is_mate_search, LoseKind::kNoMate);
    } else {
      PrintResult(is_mate_search, LoseKind::kTimeout);
    }
  }

  if (Search::Limits.mate == 0) {
    // "go infinite"に対してはstopが送られてくるまで待つ。
    while (!Threads.stop && Search::Limits.infinite) {
      Tools::sleep(1);
    }
    if (best_move == MOVE_NONE) {
      sync_cout << "bestmove resign" << sync_endl;
    } else {
      sync_cout << "bestmove " << best_move << sync_endl;
    }
    return;
  }
}

// 探索本体。並列化している場合、ここがslaveのエントリーポイント。
void Thread::search() {
  komori::InitializeThread(id(), Threads.size());
  const auto result = g_searcher->Search(rootPos, IsPosOrNode(rootPos));
  if (id() == 0) {
    g_search_result = result;
  }
}

#endif  // USER_ENGINE
