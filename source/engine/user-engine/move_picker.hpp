/**
 * @file move_picker.hpp
 */
#ifndef KOMORI_MOVE_PICKER_HPP_
#define KOMORI_MOVE_PICKER_HPP_

#include <array>

#include "initial_estimation.hpp"
#include "node.hpp"
#include "typedefs.hpp"

namespace komori {

/**
 * @brief 詰将棋探索用の指し手生成器
 *
 * 詰将棋探索に特化した指し手生成。`generateMoves()` では非合法手が混じっているので、生成時点で
 * 合法手（攻め方なら王手、玉方なら王手を逃げる手）だけになるようフィルターしている。
 *
 * @note サイズがそこそこ大きいので、再帰関数で使用する場合はスタックオーバーフローに注意すること。
 */
class MovePicker {
 public:
  /// Default constructor(delete)
  MovePicker() = delete;
  /// Copy constructor(delete)
  MovePicker(const MovePicker&) = delete;
  /// Move constructor(delete)
  MovePicker(MovePicker&& rhs) noexcept = delete;
  /// Copy assign operator(delete)
  MovePicker& operator=(const MovePicker&) = delete;
  /// Move assign operator(delete)
  MovePicker& operator=(MovePicker&& rhs) noexcept = delete;
  /// Destructor(default)
  ~MovePicker() = default;

  /**
   * @brief 局面 `n` における合法手を生成する。
   * @param n        現局面
   * @param ordering オーダリング用の評価値を計算するかどうか。（`true` だと若干遅くなる）
   */
  explicit MovePicker(const Node& n, bool ordering = false) {
    bool judge_check = false;
    ExtMove* last = nullptr;
    const bool or_node = n.IsOrNode();
    if (or_node) {
      if (n.Pos().in_check()) {
        last = generateMoves<EVASIONS_ALL>(n.Pos(), move_list_.data());
        // 逆王手になっているかチェックする必要がある
        judge_check = true;
      } else {
        last = generateMoves<CHECKS_ALL>(n.Pos(), move_list_.data());
      }
    } else {
      last = generateMoves<EVASIONS_ALL>(n.Pos(), move_list_.data());
    }

    // OrNodeで王手ではない手と違法手を取り除く
    last = std::remove_if(move_list_.data(), last, [&](const auto& m) {
      return (judge_check && !n.Pos().gives_check(m.move)) || !n.Pos().legal(m.move);
    });
    size_ = last - move_list_.data();

    // オーダリング情報を付加したほうが定数倍速くなる
    if (ordering) {
      for (auto& move : *this) {
        move.value = MoveBriefEvaluation(n, move.move);
      }
    }
  }

  /// 現局面の合法手の個数を返す。
  std::size_t size() const { return size_; }
  /// 現局面の合法手の数が0かどうかを判定する。
  bool empty() const { return size() == 0; }
  /// 合法手の先頭へのポインタ
  ExtMove* begin() { return move_list_.data(); }
  /// 合法手の先頭へのポインタ
  const ExtMove* begin() const { return move_list_.data(); }
  /// 合法手の終端+1へのポインタ
  ExtMove* end() { return begin() + size_; }
  /// 合法手の終端+1へのポインタ
  const ExtMove* end() const { return begin() + size_; }

  /// `i` 番目の合法手
  auto& operator[](std::size_t i) { return move_list_[i]; }
  /// `i` 番目の合法手
  const auto& operator[](std::size_t i) const { return move_list_[i]; }

 private:
  std::array<ExtMove, kMaxCheckMovesPerNode> move_list_;  ///< 合法手のリスト
  std::size_t size_;                                      ///< 合法手の個数
};

template <variant::Rule R>
class VariantMovePicker {
 public:
  explicit VariantMovePicker(const NodeFor<R>& n, bool ordering = false) {
    if (!n.IsOrNode() && !n.Pos().in_check()) {
      return;
    }
    variant::Generate<R>(variant::Board{n.Pos()}, n.Us(), [&](Move move, Bitboard checkers) {
      if (!n.IsOrNode() || checkers) {
        ExtMove entry{};
        entry.move = move;
        if (ordering) {
          entry.value = MoveBriefEvaluation(n, move);
        }
        moves_.push_back(entry);
      }
      return false;
    });
  }

  std::size_t size() const { return moves_.size(); }
  bool empty() const { return moves_.empty(); }
  auto begin() const { return moves_.begin(); }
  auto end() const { return moves_.end(); }
  const ExtMove& operator[](std::size_t i) const { return moves_[i]; }

 private:
  std::vector<ExtMove> moves_;
};

template <variant::Rule R>
using MovePickerFor = std::conditional_t<R == variant::Rule::standard, MovePicker, VariantMovePicker<R>>;

}  // namespace komori

#endif  // KOMORI_MOVE_PICKER_HPP_
