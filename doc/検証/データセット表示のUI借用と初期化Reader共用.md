# データセット表示のUI借用と初期化Reader共用

検証日: 2026-10-29

## 所有と処理の順序

SelectedDatasetはStorageの固定メンバーが保持する。設定画面に必要な一覧・タイトル・説明・言語の表示情報は、要求時にUIが所有する既存の境界画像back領域へ書く。Appは解析結果を永続保持せず、要求・進捗・完了の制御情報を扱う。

1. UIがBeginDatasetReadで既存back画像のアドレス・容量・貸出しIDを返す。DatasetReadLayoutが先頭にDatasetViewを構築する。
2. Appは画面世代と要求tokenを付け、DatasetOperationでStorageへ渡す。同期結果は受付成否だけを表す。
3. StorageはDatasetCatalog::readInto等で借用先へ直接書き、要求と成否だけを非同期通知する。
4. Appは通知ハンドラでは完了状態だけを記録する。通常ループからUIへCommitDatasetReadまたはDiscardDatasetReadを送り、返却を確認する。
5. UIは現在画面に対応する結果だけを既存の画面専用DatasetViewへ取り込む。通常のApp更新はこのUI所有データを上書きしない。借用型を破棄してからbackを再利用する。

新しい読出しで古い要求を差し替えた場合、先行結果の公開を禁止し、書込みの終了・返却後に新しい要求を開始する。画面切替も書込み権を取り消さない。返却失敗時は管理情報を残して再試行する。Storageが同期受付を拒否した場合は借用を返してから再要求する。地図・地名検索・データセット結果を同じbackへ同時に貸さない。

言語操作は現在言語・件数・位置と、変更方向をStorageへ問い合わせる方式を維持した。全言語リストをUIへ追加保持しない。選択行から対象番号を解決する責務はUIが持ち、Appは画面世代・確認済み操作・ログ停止などの条件を確認する。

## 初期化時のReader

CheckDatasetはStorageIndexLeaseで既存96 KiBを一時的に索引用作業領域へ切り替える。そこから4 KiBを借り、索引・GLC・Wordbookの初期確認へ順番に渡す。Readerの終了・例外で返却し、永続的なFileReadSourceには借用ポインタを保存しない。遅延初期化を伴う地名検索は、その検索に貸されたUIの4 KiBを使用する。

地図ジョブ・未返却の地図結果・FIL・未完了ログ読出しが使用中の場合はWaitingを返す。Appは選択を解除せず、処理後に再確認する。借用できた場合は両MapSessionの保持内容を無効化するため、次の地図要求は再生成する。これは設定変更や再確認時の処理であり、通常の地名検索ごとに行う処理ではない。

INI解析のstring・言語一覧、候補確認で利用するSDK Reader内部の確保は残る。初期化全体・データセット画面全体がヒープレスになったという意味ではない。今回除いたのは通常のCheckDatasetでReaderが自己確保していた4 KiBである。借用指定なしの互換APIは残す。

## 検証

以下はすべて成功した。

- `tests/run_native.py`: UI結果の貸出し、二重借用拒否、部分書込みの非公開、古いApp更新による巻戻し防止、異なる貸出しID・重複完了の拒否、画面変更後の返却、保守進捗・失敗の表示、UI側での対象番号解決。既存のUI・GPS・設定・キー・ログ・USB試験も成功。
- `tests/run_logger_app.py`: AppがDatasetViewを保持しないこと、同期待ち中に届く完了の遅延回収、要求差替え、返却失敗の再試行、Storage受付拒否からの再要求、保守進捗・完了、初期化作業域の競合時に選択を維持すること。既存のログ記録・画面切替・地図・地名・リプレイ試験も成功。
- `tests/run_storage_work.py`: Storage作業域の寿命、close失敗・取消・古い結果、100回の動的確保なしの切替。
- `tests/run_borrowed_place_files.py`: N03とestatの実データで初期確認中の4,096 BのC++確保が0件であること、借用とFILを処理後に返すことを確認。7点ずつの検索も比較し、借用側の通常検索はC++動的確保0件、区域コード・地名・キャッシュ結果が一致した。AddressSanitizerとUndefinedBehaviorSanitizerを使用。
- `tests/run_place_files.py`: N03の実ファイルによるINI・一覧・詳細・言語候補、正常・異常系の読出しと回復、324点のキャッシュ結果比較。
- PlatformIO `cardputer-adv`: ビルド成功。今回変更したファイルの差分について空白検査も成功。

実データにはN03の`N03-20260101-grid-8192-10000.remap.wgsmapset.glc`とestatの`estat-r2ka-jgd2011-2020-grid-8192-10000.remap.wgsmapset.glc`、対応するUTF-8 Wordbookを使用した。計測対象はホストのC++確保であり、VFS・FatFs・標準IOなどの内部確保全体を測定した値ではない。

## メモリの比較

同じESP32-S3向け設定の変更直前ELFと変更後ELFを比較した。サイズはtarget GDBとPlatformIOによる。

| 項目 | 変更前 | 変更後 | 差 |
|---|---:|---:|---:|
| 静的RAM | 253,740 B | 253,988 B | +248 B |
| Flash | 1,241,769 B | 1,244,701 B | +2,932 B |
| Application | 217,560 B | 217,808 B | +248 B |
| StorageTask | 175,016 B | 175,032 B | +16 B |
| UiModel | 14,016 B | 14,080 B | +64 B |
| SelectedDataset | 792 B | 792 B | 0 |
| DatasetView | 472 B | 472 B | 0 |
| DatasetRequest | 44 B | 60 B | +16 B |
| AppNotification | 816 B | 816 B | 0 |
| UiRequest | 1,424 B | 1,424 B | 0 |
| SyncReturn | 288 B | 288 B | 0 |

ApplicationはStorage・UIを内包するため、各行を加算しない。増えた固定領域は要求・貸出し・進捗の管理情報である。結果用の新しい大配列はなく、既存6,060 Bのback画像とUIの画面専用unionを再利用する。通知のDatasetView値コピーを除いても、通知unionの最大メンバーは別の結果なので、キュー1件のサイズは変わらない。

Reader用4 KiBは固定領域から借りるため初期化時の自己確保がなくなる。これは常時4 KiBの空きヒープが増えたという意味ではなく、初期化中の一時確保と断片化要因の削減である。

コンパイラが生成した個別関数のフレームでは、StorageTask::completeDatasetJobが1,072→128 B、completeJobが1,296→1,328 B、PlaceLookup::openが1,248→1,264 Bとなった。結果を借用先へ直接作ることでStorageの結果用局所変数を削減した。AppのUI同期要求は既存サイズのUiRequestを使い、専用noinline関数へ分離した。この数値は関数単体のフレームであり、呼出しを含む最大スタック消費量ではない。

実機への書込み、実機のヒープ高水位・スタック高水位・処理時間の再計測は今回実施していない。
