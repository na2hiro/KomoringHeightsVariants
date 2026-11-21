/**
 * @file expansion_stack.hpp
 */
#ifndef KOMORI_EXPANSION_STACK_HPP_
#define KOMORI_EXPANSION_STACK_HPP_

#include <deque>
#include "local_expansion.hpp"

namespace komori {
/**
 * @brief `LocalExpansion` をスタックで管理するクラス。
 *
 * 基本的には `std::stack<LocalExpansion>` のように振る舞う。`Emplace()` により新たな `LocalExpansion` を
 * 構築し、`Pop()` により構築したインスタンスのうち最も新しいものを消す。最新のインスタンスは `Current()` で取得できる。
 */
template <variant::Rule R>
class ExpansionStackFor {
  using LocalExpansion = LocalExpansionFor<R>;

 public:
  /// Default constructor(default)
  ExpansionStackFor() = default;
  /// Copy constructor(delete)
  ExpansionStackFor(const ExpansionStackFor&) = delete;
  /// Move constructor(delete)
  ExpansionStackFor(ExpansionStackFor&&) = delete;
  /// Copy assign operator(delete)
  ExpansionStackFor& operator=(const ExpansionStackFor&) = delete;
  /// Move assign operator(delete)
  ExpansionStackFor& operator=(ExpansionStackFor&&) = delete;
  /// Destructor(default)
  ~ExpansionStackFor() = default;

  /**
   * @brief スタックの先頭に `LocalExpansion` オブジェクトを構築する。
   * @tparam Args `LocalExpansion` のコンストラクタの引数。詳細は `LocalExpansion` の定義を参照。
   * @param args `LocalExpansion` のコンストラクタの引数。
   * @return 構築した `LocalExpansion` オブジェクト
   *
   * 構築した `LocalExpansion` において局面の合流を検出した場合、二重カウントの回避を試みる。
   */
  template <typename... Args>
  LocalExpansion& Emplace(Args&&... args) {
    auto& expansion = list_.emplace_back(std::forward<Args>(args)...);
    return expansion;
  }

  /**
   * @brief スタック先頭の `LocalExpansion` オブジェクトを開放する。
   */
  void Pop() noexcept { list_.pop_back(); }

  /// スタック先頭要素を返す。
  LocalExpansion& Current() { return list_.back(); }
  /// スタック先頭要素を返す。
  const LocalExpansion& Current() const { return list_.back(); }

  /// スタックが空かどうか判定する
  bool IsEmpty() const { return list_.empty(); }
  /**
   * @brief スタックの最も古い要素を返す
   * @return スタックの最も古い要素
   * @pre `!IsEmpty()`
   */
  const LocalExpansion& Root() const { return list_.front(); }

  /**
   * @brief 現局面が終点となるの二重カウント解消を試みる
   * @param tt 置換表
   * @param n  現局面
   */
  void EliminateDoubleCount(tt::TranspositionTableFor<R>& tt, const NodeFor<R>& n) {
    const auto& current = Current();
    if (current.empty()) {
      return;
    }

    const auto best_move = current.BestMove();
    if (const auto opt = FindKnownAncestor(tt, n, best_move)) {
      const auto branch_root_edge = *opt;
      for (auto itr = list_.rbegin() + 1; itr != list_.rend(); ++itr) {
        if (itr->ResolveDoubleCountIfBranchRoot(branch_root_edge)) {
          break;
        }

        if (itr->ShouldStopAncestorSearch(branch_root_edge.branch_root_is_or_node)) {
          break;
        }
      }
    }
  }

 private:
  /**
   * @brief 格納データの本体。
   *
   * @note `std::vector` のほうが若干高速に動作すると思われるが、
   *       `LocalExpansion` のような move 不可オブジェクトには用いることができない。
   */
  std::deque<LocalExpansion> list_;
};
using ExpansionStack = ExpansionStackFor<variant::Rule::standard>;
}  // namespace komori

#endif  // KOMORI_EXPANSION_STACK_HPP_
