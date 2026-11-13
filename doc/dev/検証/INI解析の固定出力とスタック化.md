# INI解析の固定出力とスタック化

検証日: 2026-10-29

## 実装

INI解析のDatasetMetadataから6本のstringとvector<string>を除去した。readDatasetMetadataIntoは呼出し元の固定結果へ地図名・辞書名・選択言語・書式・言語件数・選択位置を出力する。タイトルと説明は別の出力指定により、UIが貸したDatasetViewの既存配列へ直接書く。実行用の結果には表示文字列を重複保持しない。

解析関数は16件×32 Bの言語ID本体512 B、件数1 B、256 Bの行、32 Bずつの現在セクション・既定言語とカウンタを局所スタックに持つ。Config.hのMaxDatasetLanguagesを16とし、17個目はMetadataエラーとして拒否する。動的確保へ退避しない。重複検出、全言語の必須項目・UTF-8・書式の検査、保存済み言語の存在確認は維持する。言語切替の方向は同じ2走査内で処理し、従来の「現在言語を解析→隣を選択→再解析」の4走査を2走査へ減らした。

通常の返却結果に言語一覧を含めない。全言語の索引作成で必要な場合だけ、索引処理内の局所一覧へコピーを要求し、辞書情報の列挙に使う。索引処理のBookSource vector・文字列と進捗用文字列のヒープは今回残している。列挙処理はgenerateより前に戻るため、この局所一覧のフレームは後続のTI00展開へ重ならない。

初期化のPlaceLookup::openはnoinlineのreadSelectionを呼び、解析結果をSelectedDatasetへ反映して戻ってから索引・GLC・WordbookのReader確認へ進む。解析関数の行・言語一覧と、readSelectionの一時結果のフレームはReader処理に持ち越さない。索引生成のWordbook処理も必要な言語・辞書名だけを受け、大きなDatasetMetadataの複製を保持しない。

INIの形式バージョン、NVSと保存索引の形式は変更しない。標準IO内部の確保、候補確認に使うSDK Readerの内部確保は対象外であり、Storage全体のヒープ廃止を意味しない。Core・SDKは変更していない。

## 検証

以下はすべて成功した。

- `tests/run_metadata.py`: C++のoperator new/new[]を計数し、解析中の確保0件を確認。16件の受入れ・17件目の拒否・重複、31 Bの言語ID・127 Bのファイル名、前後の言語切替と折返し、UTF-8境界での省略、連続操作時の出力初期化、不正UTF-8と存在しない言語、出力先を持たない実行用解析を検査。AddressSanitizerとUndefinedBehaviorSanitizerを併用。
- `tests/run_native.py`: 上記試験を組み込み、既存のINI検証・UI・言語・設定・GPS・ログ・USB・借用の試験も成功。以前のINI検証用の表示スナップショットはテスト側のアダプタで作り、製品コードの固定結果と区別した。
- `tests/run_place_files.py`: N03実データで、候補一覧・詳細・言語切替・適用候補の確認と、324点のキャッシュ結果比較が成功。
- `tests/run_borrowed_place_files.py`: N03とestatの実データで、INIを含む借用側の初期確認がC++確保0件。4 KiBとFILが返ることも確認。各7点の通常検索は引き続きC++確保0件で、区域コード・地名・キャッシュ結果が一致。
- `tests/run_dataset_index.py`: 実データと合成データの全言語生成・読取り専用突合・全再生成・失敗時の復旧・31 Bの言語ID・4 KiB借用・CRCの独立検証が成功。既存索引形式の内容を維持した。
- `tests/run_logger_app.py`: 通常のログ記録・GPS操作・画面遷移・借用・データセット操作・ログ閲覧等が成功。
- PlatformIO `cardputer-adv`ビルドと、今回変更したファイルの空白検査が成功。

## メモリ

変更直前のELFと、同じESP32-S3向け設定の変更後ELFを比較した。サイズはPlatformIOとtarget GDBによる。

| 項目 | 変更前 | 変更後 | 差 |
|---|---:|---:|---:|
| 静的RAM | 253,988 B | 253,988 B | 0 |
| Flash | 1,244,701 B | 1,243,541 B | −1,160 B |
| Application | 217,808 B | 217,808 B | 0 |
| StorageTask | 175,032 B | 175,032 B | 0 |
| DatasetMetadata | 416 B | 552 B | +136 B |

旧DatasetMetadataの416 Bはヒープ側の文字列・vectorの実データを含まない。新しい552 Bにはファイル名・言語・書式等の実データを含み、操作の局所結果として使う。永続オブジェクトへ新しい解析用メンバーを追加していないため、常設RAMは増えない。

コンパイラの個別関数フレームは次のとおり。関数の呼出しを含む最大使用量ではなく、実機のスタック高水位とも区別する。

| 関数 | 変更前 | 変更後 |
|---|---:|---:|
| INI解析 | 944 B | 1,088 B |
| DatasetCatalog::readInto | 1,872 B | 1,888 B |
| PlaceLookup::open | 1,264 B | 784 B |
| PlaceLookup::readSelection | なし | 1,296 B |
| StorageTask::maintainDatasetIndex | 832 B | 1,024 B |
| StorageTask::applyDatasetSelection | 864 B | 1,072 B |
| bookSources | 912 B | 1,584 B |
| generate | 1,616 B | 1,248 B |

bookSourcesの増加は全言語列挙中だけの局所一覧と固定結果による。generateへ入る前に戻り、再帰を含むWordbook索引生成へ持ち越さない。通常初期化も解析側とReader側を関数で分離した。一方、解析の呼出し中には呼出し元のフレームと解析フレームが重なるため、個別フレームの表をそのままタスクの余裕量として解釈しない。

実機への書込み、実機での処理時間・空きヒープ・スタック高水位の再計測は今回実施していない。
