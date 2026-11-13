# 実機用マップセット

`mapsets/<識別名>/`はCardputerのSDカードへコピーするためのデータセットである。識別名は配布時の管理用であり、端末の番号ではない。各セットには次の実行用ファイルだけを置く。

```text
dataset.ini
map.glc
<英字Wordbook>.en.giswordbook
<日本語Wordbook>.giswordbook  # 日本の3セットのみ
```

GLCは対応するSDKリリースのうち、ヘッダーの `unit_inv_x`・`unit_inv_y` が等しく、チャンク数がファームの上限1500件以内である候補から、最も細かいものを1つ選ぶ。採用元のファイル名、解像度、チャンク数は[選定一覧](mapsets-selection.tsv)に記録する。低解像度のGLC、元言語が日本語・英語以外のWordbook、名前の対照表、チェックサム、SDKの説明書は実機用セットへ収録しない。英字Wordbookは印字可能ASCIIの言語パック版を使う。

e-Stat 2020はrev2の地図と4階層 `subdiv` の日本語・英字Wordbookを使う。3階層辞書は収録しない。

端末は `/sd/dataset/01/`〜`/sd/dataset/99/` を認識する。利用する際は空いている番号を決め、選んだセットの**中身**をその番号のディレクトリへコピーする。番号の事前割当は行わない。設定画面でデータセットを適用すると、端末が `_mapindex.dat` を生成する。元リリースと利用条件は[構築元一覧](../../tools/mapsets_sources.tsv)からSDKの対応ZIPにたどれる。

再生成はリポジトリのルートから `python3 tools/build_mapsets_from_sdk.py --output <空の出力先> --selection-manifest <選定一覧の出力先>` で行う。`mapsets/` の現物はこのスクリプトで生成する。

配布時は[ローカルリリースの作成](release.md)に従い、マップセットごとに1つのZIPを作る。各ZIPには、そのマップに対応するすべてのWordbookを同梱する。
