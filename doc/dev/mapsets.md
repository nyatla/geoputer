# 実機用マップセット

`mapsets/<識別名>/`はCardputerのSDカードへコピーするためのデータセットである。識別名を端末でもそのまま使う。各セットには次の実行用ファイルだけを置く。

```text
dataset.ini
map.glc
<英字Wordbook>.en.giswordbook
<日本語Wordbook>.giswordbook  # 日本の3セットのみ
```

GLCは対応するSDKリリースのうち、ヘッダーの `unit_inv_x`・`unit_inv_y` が等しく、チャンク数がファームの上限1500件以内である候補から、最も細かいものを1つ選ぶ。採用元のファイル名、解像度、チャンク数は[選定一覧](mapsets-selection.tsv)に記録する。低解像度のGLC、元言語が日本語・英語以外のWordbook、名前の対照表、チェックサム、SDKの説明書は実機用セットへ収録しない。英字WordbookはSDK v0.2.8の印字可能ASCII言語パックrev2を使う。地図ZIPは[構築元一覧](../../tools/mapsets_sources.tsv)に記載した版を使う。

e-Stat 2020はrev2の地図と4階層 `subdiv` の日本語・英字Wordbookを使う。3階層辞書は収録しない。

端末は `/sd/geoputer/dataset/<識別名>/` を認識する。識別名は小文字ASCIIで始まり、小文字・数字・ハイフンのみを使う1〜15文字とし、ハイフンの連続・末尾使用は認めない。`mapsets/<識別名>/` をSDカードの`/geoputer/dataset/<識別名>/`へコピーする。設定画面でデータセットを適用すると、端末が `_mapindex.dat` を生成する。元リリースと利用条件は[構築元一覧](../../tools/mapsets_sources.tsv)からSDKの対応ZIPにたどれる。

再生成はリポジトリのルートから `python3 tools/build_mapsets_from_sdk.py --output <空の出力先> --selection-manifest <選定一覧の出力先>` で行う。`mapsets/` の現物はこのスクリプトで生成する。

配布時は[ローカルリリースの作成](release.md)に従い、マップセットごとに1つのZIPを作る。各ZIPには、そのマップに対応するすべてのWordbookとREADME・NOTICEをディレクトリを設けず直下に同梱する。展開後のディレクトリ名を識別名として、SDカードの`/geoputer/dataset/`へコピーできる。
