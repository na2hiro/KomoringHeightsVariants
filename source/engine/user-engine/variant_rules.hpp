#ifndef KOMORI_VARIANT_RULES_HPP_
#define KOMORI_VARIANT_RULES_HPP_

#include <array>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

#include "../../position.h"

namespace komori::variant {

// Values follow the rule IDs used by Haitaka Variants and Shogitter.
enum class Rule {
  standard = 0,
  annan = 26,
  anhoku = 55,
  antouzai = 95,
  taimen = 72,
  haimen = 74,
  anki = 94,
  tenjiku = 56,
  neko = 130,
  nekoneko = 131,
  yokoneko = 132,
  yokonekoneko = 133,
  tenkyo = 151,
};

struct RuleInfo {
  Rule rule;
  std::string_view name;
};

inline constexpr std::array kRules{
    RuleInfo{Rule::standard, "standard"}, RuleInfo{Rule::annan, "annan"},
    RuleInfo{Rule::anhoku, "anhoku"},     RuleInfo{Rule::antouzai, "antouzai"},
    RuleInfo{Rule::taimen, "taimen"},     RuleInfo{Rule::haimen, "haimen"},
    RuleInfo{Rule::anki, "anki"},         RuleInfo{Rule::tenjiku, "tenjiku"},
    RuleInfo{Rule::neko, "neko"},         RuleInfo{Rule::nekoneko, "nekoneko"},
    RuleInfo{Rule::yokoneko, "yokoneko"}, RuleInfo{Rule::yokonekoneko, "yokonekoneko"},
    RuleInfo{Rule::tenkyo, "tenkyo"},
};

inline constexpr char kAnnanStartpos[] = "lnsgkgsnl/1r5b1/p1ppppp1p/1p5p1/9/1P5P1/P1PPPPP1P/1B5R1/LNSGKGSNL b - 1";

#ifdef KOMORI_FIXED_RULE
inline constexpr Rule kDefaultRule = Rule::KOMORI_FIXED_RULE;
#else
inline constexpr Rule kDefaultRule = Rule::standard;
#endif

inline const char* Startpos(Rule rule) {
  return rule == Rule::annan ? kAnnanStartpos : SFEN_HIRATE.c_str();
}

namespace detail {
template <std::size_t I, typename F>
decltype(auto) DispatchRule(Rule rule, const F& f) {
  constexpr Rule candidate = kRules[I].rule;
  if constexpr (I + 1 == kRules.size()) {
    return f(std::integral_constant<Rule, candidate>{});
  } else {
    if (rule == candidate) {
      return f(std::integral_constant<Rule, candidate>{});
    }
    return DispatchRule<I + 1>(rule, f);
  }
}
}  // namespace detail

template <typename F>
decltype(auto) Dispatch(Rule rule, const F& f) {
#ifdef KOMORI_FIXED_RULE
  (void)rule;
  return f(std::integral_constant<Rule, kDefaultRule>{});
#else
  return detail::DispatchRule<0>(rule, f);
#endif
}

// Temporary physical board for testing candidate moves, including pawn-drop replies.
// The search position and its state chain are updated only for moves actually played.
class Board {
 public:
  explicit Board(const Position& pos)
      : pieces_{pos.pieces(BLACK), pos.pieces(WHITE)},
        kings_{pos.king_square(BLACK), pos.king_square(WHITE)},
        hands_{pos.hand_of(BLACK), pos.hand_of(WHITE)} {
    for (const auto sq : Occupied()) {
      board_[sq] = pos.piece_on(sq);
    }
  }

  Piece At(Square sq) const { return sq == SQ_NB ? NO_PIECE : board_[sq]; }
  Square King(Color c) const { return kings_[c]; }
  Hand HandOf(Color c) const { return hands_[c]; }
  Bitboard Pieces(Color c) const { return pieces_[c]; }
  Bitboard Occupied() const { return pieces_[BLACK] | pieces_[WHITE]; }

  void Play(Color us, Move move) {
    const auto to = to_sq(move);
    if (is_drop(move)) {
      sub_hand(hands_[us], move_dropped_piece(move));
    } else {
      const auto from = from_sq(move);
      if (type_of(board_[from]) == KING) {
        kings_[us] = to;
      }
      board_[from] = NO_PIECE;
      pieces_[us] ^= from;
      if (const auto captured = board_[to]; captured != NO_PIECE) {
        pieces_[~us] ^= to;
        add_hand(hands_[us], raw_type_of(captured));
      }
    }
    board_[to] = static_cast<Piece>(move >> 16);
    pieces_[us] |= to;
  }

 private:
  std::array<Piece, SQ_NB> board_{};
  std::array<Bitboard, COLOR_NB> pieces_;
  std::array<Square, COLOR_NB> kings_;
  std::array<Hand, COLOR_NB> hands_;
};

inline Square Offset(Square sq, int df, int dr) {
  const int file = file_of(sq) + df;
  const int rank = rank_of(sq) + dr;
  if (file < 0 || file >= FILE_NB || rank < 0 || rank >= RANK_NB) {
    return SQ_NB;
  }
  return static_cast<File>(file) | static_cast<Rank>(rank);
}

template <Rule R>
unsigned MovementTypes(const Board& board, Color us, Square from) {
  constexpr bool reflects_run =
      R == Rule::neko || R == Rule::nekoneko || R == Rule::yokoneko || R == Rule::yokonekoneko;
  const unsigned native = 1u << type_of(board.At(from));
  unsigned borrowed = 0;
  const auto borrow = [&](Square sq, Color color) {
    const auto donor = board.At(sq);
    if (donor != NO_PIECE && color_of(donor) == color) {
      borrowed |= 1u << type_of(donor);
    }
  };

  if constexpr (R == Rule::annan || R == Rule::anhoku || R == Rule::taimen || R == Rule::haimen || R == Rule::tenjiku) {
    constexpr bool behind = R == Rule::annan || R == Rule::haimen || R == Rule::tenjiku;
    constexpr bool enemy = R == Rule::taimen || R == Rule::haimen;
    const int forward = us == BLACK ? -1 : 1;
    borrow(Offset(from, 0, behind ? -forward : forward), enemy ? ~us : us);
  } else if constexpr (R == Rule::antouzai) {
    borrow(Offset(from, -1, 0), us);
    borrow(Offset(from, 1, 0), us);
  } else if constexpr (R == Rule::anki) {
    for (const int sign_file : {-1, 1}) {
      for (const int sign_rank : {-1, 1}) {
        borrow(Offset(from, sign_file, 2 * sign_rank), us);
        borrow(Offset(from, 2 * sign_file, sign_rank), us);
      }
    }
  } else if constexpr (R == Rule::tenkyo) {
    const auto donor = board.At(Inv(from));
    if (donor != NO_PIECE) {
      borrowed = 1u << type_of(donor);
    }
  } else if constexpr (reflects_run) {
    constexpr bool horizontal = R == Rule::yokoneko || R == Rule::yokonekoneko;
    constexpr bool mixed = R == Rule::nekoneko || R == Rule::yokonekoneko;
    const auto along = [&](int step) { return horizontal ? Offset(from, step, 0) : Offset(from, 0, step); };
    const auto in_run = [&](int step) {
      const auto pc = board.At(along(step));
      return pc != NO_PIECE && (mixed || color_of(pc) == us);
    };
    int left = 0;
    int right = 0;
    while (in_run(left - 1)) {
      --left;
    }
    while (in_run(right + 1)) {
      ++right;
    }
    borrowed = 1u << type_of(board.At(along(left + right)));
  }
  if constexpr (R == Rule::tenjiku) {
    return native | borrowed;
  }
  return borrowed ? borrowed : native;
}

template <Rule R>
Bitboard Effects(const Board& board, Color us, Square from, const Bitboard& occupied) {
  unsigned types = MovementTypes<R>(board, us, from);
  Bitboard effects = ZERO;
  while (types) {
    const auto pt = static_cast<PieceType>(pop_lsb(types));
    effects |= effects_from(make_piece(us, pt), from, occupied);
  }
  return effects;
}

template <Rule R>
Bitboard Checkers(const Board& board, Color defender) {
  const auto king = board.King(defender);
  if (king == SQ_NB) {
    return ZERO;
  }
  const auto occupied = board.Occupied();
  const auto candidates = (rookEffect(king, occupied) | bishopEffect(king, occupied) | knightEffect(defender, king)) &
                          board.Pieces(~defender);
  Bitboard checkers = ZERO;
  for (const auto from : candidates) {
    if (Effects<R>(board, ~defender, from, occupied) & king) {
      checkers |= from;
    }
  }
  return checkers;
}

template <Rule R>
bool DeadSquare(PieceType pt, Color us, Square to) {
  if constexpr (R == Rule::anhoku || R == Rule::taimen) {
    // There is no square in front of the last rank to provide new movement.
    return pt >= PAWN && pt <= KNIGHT && relative_rank(us, rank_of(to)) == RANK_1;
  }
  return false;
}

inline bool HasPawnOnFile(const Board& board, Color us, Square to, Square except = SQ_NB) {
  const auto pawn = make_piece(us, PAWN);
  for (const auto sq : board.Pieces(us) & file_bb(file_of(to))) {
    if (sq != except && board.At(sq) == pawn) {
      return true;
    }
  }
  return false;
}

template <Rule R>
bool CanLand(const Board& board, Color us, PieceType pt, Square from, Square to, bool promote) {
  if (promote) {
    return PAWN <= pt && pt <= ROOK && canPromote(us, from, to);
  }
  return !DeadSquare<R>(pt, us, to) && (pt != PAWN || !HasPawnOnFile(board, us, to, from));
}

template <Rule R>
bool HasLegalMove(const Board& board, Color us);

template <Rule R>
std::optional<Bitboard> LegalAfter(const Board& board, Color us, Move move) {
  auto after = board;
  after.Play(us, move);
  if (Checkers<R>(after, us)) {
    return std::nullopt;
  }
  const auto checkers = Checkers<R>(after, ~us);
  if (is_drop(move) && move_dropped_piece(move) == PAWN && (checkers & to_sq(move)) && !HasLegalMove<R>(after, ~us)) {
    return std::nullopt;
  }
  return checkers;
}

template <Rule R>
std::optional<Bitboard> TryMove(const Position& pos, Move move) {
  const auto to = to_sq(move);
  if (!is_ok(move) || !is_ok(to)) {
    return std::nullopt;
  }
  const auto us = pos.side_to_move();
  const Board board{pos};
  if (is_drop(move)) {
    const auto pt = move_dropped_piece(move);
    if (pt < PAWN || pt > GOLD || !hand_count(board.HandOf(us), pt) || board.At(to) != NO_PIECE ||
        DeadSquare<R>(pt, us, to) || (pt == PAWN && HasPawnOnFile(board, us, to))) {
      return std::nullopt;
    }
  } else {
    const auto from = from_sq(move);
    if (!is_ok(from) || !(board.Pieces(us) & from) || (board.Pieces(us) & to) || to == board.King(~us) ||
        !CanLand<R>(board, us, type_of(board.At(from)), from, to, is_promote(move)) ||
        !(Effects<R>(board, us, from, board.Occupied()) & to)) {
      return std::nullopt;
    }
  }
  return LegalAfter<R>(board, us, move);
}

// Callback returns true to terminate enumeration early.
template <Rule R, typename F>
bool Generate(const Board& board, Color us, const F& visit) {
  const auto occupied = board.Occupied();
  const auto targets = ~(board.Pieces(us) | Bitboard(board.King(~us)));
  const auto accept = [&](Move move) {
    const auto checkers = LegalAfter<R>(board, us, move);
    return checkers && visit(move, *checkers);
  };

  for (const auto from : board.Pieces(us)) {
    const auto pc = board.At(from);
    const auto pt = type_of(pc);
    for (const auto to : Effects<R>(board, us, from, occupied) & targets) {
      if (CanLand<R>(board, us, pt, from, to, false) && accept(make_move(from, to, pc))) {
        return true;
      }
      if (CanLand<R>(board, us, pt, from, to, true) && accept(make_move_promote(from, to, pc))) {
        return true;
      }
    }
  }
  for (PieceType pt = PAWN; pt <= GOLD; ++pt) {
    if (!hand_count(board.HandOf(us), pt)) {
      continue;
    }
    for (const auto to : ~occupied) {
      if (!DeadSquare<R>(pt, us, to) && (pt != PAWN || !HasPawnOnFile(board, us, to)) &&
          accept(make_move_drop(pt, to, us))) {
        return true;
      }
    }
  }
  return false;
}

template <Rule R>
bool HasLegalMove(const Board& board, Color us) {
  return Generate<R>(board, us, [](Move, Bitboard) { return true; });
}

template <Rule R>
void DoMove(Position& pos, Move move, StateInfo& state) {
  if constexpr (R == Rule::standard) {
    pos.do_move(move, state);
  } else {
    const auto us = pos.side_to_move();
    Board after{pos};
    after.Play(us, move);
    pos.do_move_with_checkers(move, state, Checkers<R>(after, ~us));
  }
}

template <Rule R>
bool VerifyMate(const Position& pos, bool root_is_attacker, const std::vector<Move>& moves) {
  Board board{pos};
  auto us = pos.side_to_move();
  const auto attacker = root_is_attacker ? us : ~us;
  for (const auto move : moves) {
    if (!Generate<R>(board, us,
                     [&](Move legal, Bitboard checkers) { return legal == move && (us != attacker || checkers); })) {
      return false;
    }
    board.Play(us, move);
    us = ~us;
  }
  return us != attacker && Checkers<R>(board, us) && !HasLegalMove<R>(board, us);
}

}  // namespace komori::variant

#endif  // KOMORI_VARIANT_RULES_HPP_
