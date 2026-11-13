# 候補確認のSD Reader統一

検証日: 2026-10-29

## 実装と所有関係

データセットの詳細・言語候補・適用前確認から、SDKのGaluchatGisWordBookReaderインスタンスとObservedFileReaderFactoryの生成を除去した。候補は保存索引を必要としないDatasetIndex::inspectWordBookで標準ヘッダーを確認する。初期化のcheckWordBookも同じWordBookHeaderReaderを使い、その結果と元ファイル識別情報を保存索引へ照合する。

StorageTaskは一覧以外の候補確認でStorageIndexLeaseを取得し、既存96 KiB内の4 KiB×2本をGLC確認へ貸す。GLCの2本のSourceを閉じてから、Wordbookの1本のSourceへ同じ領域を貸す。Reader本体と84 BのWordBookHeaderは局所スタックに置く。タイトル・説明などは従来のUI借用先へ直接出力し、新しい表示用バッファを追加しない。一覧はINIだけを読み、96 KiBを借りない。空き領域を確保できない時はBusyを返す。

96 KiBの所有者と参照先はStorageタスク内だけである。このアドレスをApp・UIへ送らない。タスク間で渡す借用アドレスはUI所有のback画像で、UI→App→Storageの要求と、Storage→App→UIの完了を通して返す。Storage内の96 KiBの貸出しと、UIから借りる結果領域は独立して管理する。

96 KiBを借りる時は保持中の通常地図・ログ地図を無効化し、終了・失敗時に配列を再結合する。次の地図表示では再生成する。使用中のSelectedDataset、地名判定用64 KiB、地名キャッシュ、NVSの選択は候補確認だけでは変更しない。

## Wordbookの確認

WordBookHeaderReaderはGW00・TM00・TT00・TI00の順序、ファイル・各チャンクの境界、GW00の形式識別子と任意メタデータ長、TM00・TT00・TI00の標準ヘッダーを読む。TTのトークンビット幅1〜16、TIのコードビット幅1〜31、TIヘッダーの必須非ゼロ値とストリーム長、TMとTT・TTとTIのビット容量の対応も確認する。各ヘッダーはLimitedReaderでチャンク内へ制限し、不足を次のチャンクから補って読み進めない。

GW00の任意メタデータは内容を解釈・保持せず、次チャンクへシークする。メタデータの長さに比例した配列・stringを作らない。標準ヘッダーの確認は全トークンのデコード・UTF-8検証を意味せず、実際の地名は引き続き読出し時に検証する。保存索引との同一性を確認するヘッダーCRCの範囲は変更しない。

SDK・Coreは変更していない。新しいReaderはアプリ側のヘッダー確認であり、SDKの展開処理は再利用する。

## Reader経路の監査

| 本番処理 | 現在の経路 |
|---|---|
| GLCの矩形読出し・区域コード検索 | FileReadSourceからObservedFileReaderをスタックに構築し、ReadFile経由でSD専用FatFsを使用 |
| Wordbookの地名検索 | DiskIndexedWordBookReaderがFileReadSourceを参照し、索引と本体を順に読む。展開にはSDKの静的処理を利用 |
| Wordbookの候補・起動確認 | WordBookHeaderReaderが借用バッファ付きDatasetIndex::Sourceを使用 |
| 索引の適用・再生成・検証 | DatasetIndex::Source／Outputと固定作業領域・固定FIL |
| INI解析 | 既存のfopen・fgetc。アプリ側の文字列・言語一覧は固定出力と局所スタック |
| GPSログ・一覧・エクスポート等 | 既存のLogFile／ReadFile・ディレクトリ操作・一部標準IO |

本番の地図・Wordbook・索引・候補確認でSDKのReaderFactoryからファイルReaderを生成する呼出しは残っていない。ObservedFileReaderFactoryとScopedReaderSourceのFactory対応はテスト・性能比較用の互換定義として残す。SDKの静的な展開関数、スタック上のチャンクデコーダー、INIやログ等の別のファイルI/Oも残る。「システム中のReaderという名前の型やSDKコードをすべて削除した」という意味ではない。

本番ELFの定義済みシンボルでは、ObservedFileReaderFactory関連とSDKのWordbook Readerコンストラクタが変更前の15件から0件になった。ソースの呼出し確認と合わせ、旧Factory経由の実行経路が本番へ残っていないことを確認した。

## 検証

以下はすべて成功した。

- tests/wordbook_header_tests.cppをrun_dataset_index.pyへ追加。SDKの従来コンストラクタを独立した比較対象にし、正常・空メタデータ・不正な識別子・長さ・順序・切詰め・追記、TM／TTのヘッダー不足、ビット幅・容量、TIの必須値とストリーム長を比較。
- 96 KiBを超える128 KiBの任意メタデータでC++確保0件を確認。同じ作業領域で異常から回復し、全ケースでFILとバッファが返ることを確認。
- run_dataset_index.pyのN03実データ・合成データで、既存の索引形式、全言語生成、元データ突合、障害時復旧と正常操作のC++確保0件を維持。
- run_place_files.pyで、N03の候補詳細と前後の言語候補を96 KiBから借用して確認。C++確保0件、表示情報の一致、欠落時の返却、保持地図の無効化と配列の再結合、地名判定用配列の維持を検査。既存の324点比較と正常・異常系試験も成功。
- run_borrowed_place_files.pyで、N03・estatの初期確認と通常検索の確保0件、地名・区域コードの一致と例外時返却を維持。
- run_storage_work.py、run_native.py、run_logger_app.pyとcardputer-advビルドが成功。今回変更したファイルの空白検査も成功。

計数対象はC++のoperator new/new[]である。標準IO・FatFs・NVS等の内部確保と、異常時の例外生成は別に扱う。

## メモリとスタック

同じESP32-S3向け設定で、変更直前ELFと変更後ELFをPlatformIO・target GDB・逆アセンブルで比較した。

| 項目 | 変更前 | 変更後 | 差 |
|---|---:|---:|---:|
| 静的RAM | 253,988 B | 253,988 B | 0 |
| Flash | 1,243,977 B | 1,239,545 B | −4,432 B |
| Application | 217,808 B | 217,808 B | 0 |
| StorageTask | 175,032 B | 175,032 B | 0 |
| StorageMapMemory | 98,308 B | 98,308 B | 0 |
| DatasetIndexWorkspace | 22,948 B | 22,948 B | 0 |
| DatasetCatalog | 36 B | 40 B | +4 B |
| WordBookHeader | なし | 84 B | 局所結果 |

DatasetCatalogの増分は作業領域への参照で、操作内の局所オブジェクトである。永続オブジェクトに新しい配列を置かず、静的RAMは増えない。

関数単体のフレームはDatasetCatalog::readIntoが1,888→1,664 B、checkWordBookが304→272 B、StorageTask::completeDatasetJobが128→192 B。新しいWordBookHeaderReader::readは96 B、inspectWordBookは240 B。フレームの値は呼出し全体の最大スタック使用量ではなく、実機の高水位とも区別する。

実機への書込みと、実機での速度・空きヒープ・スタック高水位の再計測は今回実施していない。
