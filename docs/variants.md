# 性能変化系の変則将棋

Haitaka Variants の9×9性能変化系ルールに対応しています。通常の SFEN と USI 指し手表記で局面を指定し、`go mate` または `go` で王手を続ける詰みを探索できます。

| ルール名 | ビルド・USI での値 | どの駒の利きを借りるか |
| --- | --- | --- |
| 安南 | `annan` | 直後の味方 |
| 安北 | `anhoku` | 直前の味方 |
| 安東西 | `antouzai` | 真横の味方。両側にいれば両方 |
| 対面 | `taimen` | 直前の敵 |
| 背面 | `haimen` | 直後の敵 |
| 安騎 | `anki` | チェスのナイトの位置にいる味方すべて |
| 天竺 | `tenjiku` | 自身の利きと直後の味方の利き |
| ネコ | `neko` | 同じ筋で連続する味方の列を逆順にした対応位置 |
| ネコネコ | `nekoneko` | 同じ筋で連続する敵味方の列を逆順にした対応位置 |
| 横ネコ | `yokoneko` | 同じ段で連続する味方の列を逆順にした対応位置 |
| 横ネコネコ | `yokonekoneko` | 同じ段で連続する敵味方の列を逆順にした対応位置 |
| 点鏡 | `tenkyo` | 5五を中心とする点対称の升にいる駒 |

借りた利きは動く駒の向きで適用します。成り、駒取り、二歩と持駒には実際の駒種を用います。安北と対面では歩・香・桂の一番奥への駒打ちと不成を禁じます。残り10ルールでは、その位置でも別の駒から利きを借りられるため、行き所の制限を設けません。

## ビルドと利用

```sh
cd source
make normal TARGET_CPU=AVX2 COMPILER=g++ RULESET=annan
make normal TARGET_CPU=AVX2 COMPILER=g++ RULESET=all
```

既定のビルドは本将棋固定の `KomoringHeights-by-gcc` を作ります。`RULESET=annan` は安南固定の `KomoringHeights-by-gcc-annan` を作ります。他のルールの固定版は表の値でビルドできます。`RULESET=all` は USI の `RuleSet` オプションを持つ切り替え版 `KomoringHeights-by-gcc-all` を作ります。切り替え版の既定値は本将棋です。

```text
usi
setoption name RuleSet value annan
isready
position sfen 8k/9/9/6B2/9/9/9/9/9 b G 1
go mate 1000
```

`usi` の後は `usiok` を、`isready` の後は `readyok` を待ちます。この例は `checkmate G*1b` を返します。ルールを切り替えるときは、`setoption`、`isready`、`position` の順に送ります。`position startpos` は安南の専用初期局面、または選択ルールの平手初期局面を読み込みます。

片玉の詰将棋では攻方の玉を省略できます。玉方の持駒は SFEN に明記してください。双玉で手番側が王手を受けているときは `RootIsAndNodeIfChecked` で探索開始時の役割を選びます。

## 検証

```sh
python3 script/verify_variants.py source/KomoringHeights-by-gcc-all
python3 script/verify_variants.py source/KomoringHeights-by-gcc-all --oracle-dir /path/to/oracles --positions 1024
```

`--oracle-dir` には Haitaka Variants の `87a106b` で `cargo build -p haitaka --release --example rule_oracle --features <rule>` を実行し、各ルール名で保存したバイナリを置きます。安東西でピンに見える駒が、相手に飛車や角の利きを与えている駒を取って王手筋を消せるケースは、着手後の盤面を Haitaka に再入力して検証します。

12,245局面を照合し、参照実装が生成した618,829着手は着手後の SFEN まで一致しました。安東西で追加された着手10件は上記の方法で確認しました。

## 実行時間の比較

Linux AVX2、Intel Core i7-6700、GCC 15.2.0、1スレッド、`USI_Hash=64` で計測しました。切り替え版の本将棋は現行版 `f1627f48` と、各変則ルールは対応する固定版と比較しました。15,000ノード到達時点の推定時間と固定問題を解く時間をそれぞれ30組交互に計測し、所要時間比の95%信頼区間の上限が全比較で1.02以下なら切り替え版を採用する基準としました。今回の計測では基準を満たせなかったため、固定版を既定にしました。

```sh
python3 script/benchmark_variants.py /path/to/KomoringHeights-by-gcc-all \
  /path/to/baseline/KomoringHeights-by-gcc /path/to/fixed-builds ~/proj/haitaka-variants
```

`fixed-builds` には `annan/KomoringHeights-by-gcc-annan` のように、各ルールのサブディレクトリに固定版を置きます。比較に使う局面は Haitaka Variants の同じコミットの `dfpn_corpus.rs` を読み込みます。

所要時間比は切り替え版を比較対象で割った値で、角括弧内は95%信頼区間です。切り替え版は1,112,416バイト、今回の本将棋固定版は412,000バイトです。

| ルール | 15,000ノード | 解答時間 | 比較対象のバイト数 | 判定 |
| --- | ---: | ---: | ---: | --- |
| `standard` | 1.0752 [0.9814, 1.1779] | 1.0279 [0.9651, 1.0948] | 403,808 | 基準外 |
| `annan` | 1.0273 [0.9998, 1.0556] | 1.0425 [0.9880, 1.1001] | 358,760 | 基準外 |
| `anhoku` | 0.9738 [0.9612, 0.9865] | 1.0288 [0.9504, 1.1135] | 362,856 | 基準外 |
| `antouzai` | 0.9753 [0.9544, 0.9967] | 1.0333 [0.9904, 1.0781] | 358,760 | 基準外 |
| `taimen` | 0.9996 [0.9773, 1.0224] | 1.0106 [0.9947, 1.0266] | 362,856 | 基準外 |
| `haimen` | 1.0135 [0.9818, 1.0462] | 1.0504 [0.9542, 1.1563] | 358,760 | 基準外 |
| `anki` | 0.9943 [0.9789, 1.0100] | 1.0773 [0.9883, 1.1743] | 362,856 | 基準外 |
| `tenjiku` | 0.9926 [0.9664, 1.0195] | 1.0441 [0.9998, 1.0903] | 358,760 | 基準外 |
| `neko` | 0.9850 [0.9605, 1.0101] | 1.0133 [0.9820, 1.0455] | 358,760 | 基準外 |
| `nekoneko` | 0.9998 [0.9717, 1.0286] | 1.0286 [0.9897, 1.0691] | 358,760 | 基準外 |
| `yokoneko` | 0.9877 [0.9704, 1.0053] | 1.0049 [0.9827, 1.0277] | 362,856 | 基準外 |
| `yokonekoneko` | 1.0054 [0.9816, 1.0297] | 0.9969 [0.9758, 1.0185] | 362,856 | 基準外 |
| `tenkyo` | 0.9924 [0.9564, 1.0298] | 1.0955 [1.0277, 1.1678] | 358,760 | 基準外 |
