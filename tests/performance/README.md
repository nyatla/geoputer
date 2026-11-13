# パフォーマンス検証ツール

## 現行版の計測

`python3 tests/performance/prepare_storage_work_benchmark.py --name storage-work-current`は、通常ソースを`.pio/<計測名>/application`へコピーし、読取り専用の自動操作・診断を加える。通常版のソースと設定は変更しない。生成した`.pio/<計測名>.ini`でPlatformIOビルドする。元ソースへの比較用パッチや自己確保するReaderは使わない。

固定の習志野付近の仮想座標で、区域変更、1/1・1/2・1/4、保持区域画像の再着色、通常地図とログ地図、文字表示への切替を実行する。その後、既存ログを一覧・概要・測位点0/10/20まで読取り専用で閲覧する。ログがない／短い場合はその範囲で終了する。記録・削除・索引生成・NVS保存は行わない。

`StorageWorkMemoryProbe.h`は2048 Bスタックの採取タスクを追加し、1ms間隔とReader構築直後・区間切替時の空きヒープと最大連続空き領域を採取する。短い確保を完全には捕捉しない。計測版の値にはこの負荷を含むため、通常版の値や異なる計測器の過去値と単純な差分を取らない。区間はmap-prepare、map-read、boundary、publish、place、log-readとする。

`prepare_log_statistics_benchmark.py`はログ統計の専用試験である。`summarize_*.py`は保存済みログを集計する。`ScaledBoundaryPrototype.h`は高解像度境界抽出の独立した試験オラクルとしてのみ保持する。

## 過去の記録

旧RAM索引・Factory・自己確保Reader・旧展開処理との比較用ソースと準備スクリプトは廃止した。実測ログ・CSV・報告書は`doc/dev/検証/`に履歴として残す。過去のコマンドは当時の実装に対する記録であり、現行ソースの実行手順ではない。

比較は過去の記録であること、データセット・縮尺・測定範囲・計測器の相違を明記する。旧版を再ビルドして混在させない。

## 回帰確認

`run_observed_reader.py`（別名`run_borrowed_reader.py`）は通常版・診断版とstdio・ホストFatFsの組合せで、必須借用、範囲・ビット・短いスキップ・異常時の返却を確認する。`run_place_files.py`、`run_dataset_index.py`、`run_borrowed_place_files.py`、`run_borrowed_map_files.py`、`run_wordbook_phases.py`、`run_galuchat_ranges.py`は現行の固定資源APIを使う。SDKの従来Readerは独立した形式オラクルと互換性試験に限って使用する。

`python3 tests/performance/test_benchmark_support.py`で計測コピーの隔離と設定生成を確認する。
