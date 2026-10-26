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


## Storageの作業領域共用比較

`prepare_storage_work_benchmark.py before|after --source <保存したプロジェクト>`は通常ソースの隔離コピーへ同じ計測を加える。設定ファイルは`.pio/storage-work-before.ini`と`.pio/storage-work-after.ini`となる。固定の習志野付近の仮想座標、区域変更、キャッシュミス、1/1・1/2・1/4、ログ地図、文字表示への切替を実行し、その後に既存ログを読み取り専用で一覧・概要・測位点0/10/20まで閲覧する。ファイルの削除・記録・索引再生成・NVS設定保存は行わない。ログがない／短い場合はその範囲で終了する。

StorageWorkMemoryProbe.hは比較版だけで2048 Bスタックの採取タスクを起動し、1ms間隔とReader構築直後・段階切替時に空きヒープと最大連続空き領域を採取する。段階ごとの値はサンプル最小値であり、短い確保を完全には捕捉しない。allocatorの起動後最低値min_bootも記録する。これは通常ファームの絶対値ではなく、同じ採取負荷を加えた変更前後の比較である。通常版へ自動操作と採取タスクを含めない。

Reader借用バッファの比較は、`prepare_storage_work_benchmark.py before|after --source <比較元> --name reader-borrow-before|reader-borrow-after`で既存の比較資産と分けて保存する。計測内容・採取条件は上記と同じである。

## 小規模な確保の比較

`run_memory_three_host.py --before <変更前のプロジェクト> --output <新しい保存先> <map> <Wordbook> [<map> <Wordbook> ...]`は、同一の計測プログラムを前後のアプリへリンクし、Wordbook検索・メタデータ解析・索引検証・ログ一覧のC++確保回数と生存量を比較する。戻り値のfingerprintとWordbookのI/O回数の一致も確認する。元データは読取り専用のリンクとして使い、索引とログの試験データは保存先に生成する。保存済み結果を上書きしないため、再実行には新しい出力先を指定する。

現在の直接書込み経路のメモリ区間は、map-prepare・map-read・boundary・publish・place・log-readとする。UIの貸出し開始は読込み前のmap-prepare、生成完了から返却・確定まではpublishであり、画像の行転送区間は存在しない。`ui-hidden`は表示参照を外した診断で、固定配列の解放を意味しない。過去のtransfer区間の記録は当時の比較資料として保持する。
