# パフォーマンス検証ツール

通常ファームウェアは詳細診断を無効にする。`application/Config.h`の`DetailedDiagnostics`はビルド定義`REVERSEGEOPUTER_DIAGNOSTICS`から決まり、未指定時は0となる。詳細なGIS・GPS・I/Oログ、I/O統計、区間計時、地図のヒープ診断は、値を1にした計測用ビルドで有効になる。進捗表示・時計・タイムアウト、SD/RGアクセス通知、起動結果・エラーログは通常版でも動作する。

## 継続利用する計測

- `prepare_map_pipeline_benchmark.py`：地図のI/O・展開・画像処理を分離して計測する。`--compare-stdio`、`--compare-rle`、`--compare-raw`、`--compare-span-fill`は、保存した旧実装との比較を行う。
- `prepare_code_cache_benchmark.py`：現在のディスク索引による区域コードキャッシュを128・64・32画素で比較する。
- `../run_map_index_benchmark.py`：RAM索引とディスク索引をホストで比較する。
- `summarize_*.py`：保存済み実測ログから集計する。計測用ソースの再生成は行わない。

準備スクリプトは、このディレクトリから共通の`benchmark_support.py`を使用する。通常の`application/`と`platformio.ini`は変更しない。生成先は`.pio/<計測名>/application`、ビルド先は`.pio/<計測名>/build`とし、生成する設定ファイルだけに`-DREVERSEGEOPUTER_DIAGNOSTICS=1`を追加する。再実行時は生成ソースを更新し、前回の比較用.cppを残さない。測定ログ・バイナリ・集計結果は削除しない。

コード挿入位置が現行ソースに一致しない場合は準備を中止する。書込み・キャプチャの補助スクリプトは既存のローカル環境にあればコピーするが、通常ソースの準備には必須としない。準備処理の隔離・再実行と設定分離は`python3 tests/performance/test_benchmark_support.py`で確認する。

## 過去の比較を再現する資料

`prepare_scaled_benchmark.py`、`prepare_decimated_benchmark.py`、`QuarterScaleBenchmark.h`、`ScaledBoundaryPrototype.h`および`baseline/`は、縮小順序・RAW・RLE・FatFs導入前後を比較した際の基準実装である。本番で使う別経路ではなく、再測定用の資料として保持する。準備・背景待ち・診断設定は継続利用する計測と同じ共通処理を使用する。

Galuchatヘッダーへの比較用の変更は`.pio/<計測名>/include`のコピーだけに行う。通常アプリが使用する`third_party/galuchat/`、CoreおよびSDKは変更しない。

## 通常版と診断版の回帰確認

`python3 tests/run_observed_reader.py`は両モード・両バックエンドで実行する。通常版では計時・I/O統計が更新されないことと、読出し結果・ビット単位スキップ・異常検出・SDアクセス通知が維持されることを確認する。診断版は既存のバッファサイズ・シーク・範囲制限・I/O回数の試験を実行する。実ファイルを用いる`run_place_files.py`とホストの索引比較も、計測結果を検証するため明示的に診断を有効にする。
