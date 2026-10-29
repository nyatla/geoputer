# 選択中データセット情報のStorage所有化

検証日: 2026-10-29

## 所有関係

SelectedDatasetの固定領域をApplicationからStorageTaskへ移した。Storageは外部のSelectedDataset参照をコンストラクタで受け取らず、自身の固定メンバーを初期化・更新・無効化する。PlaceLookupと地図読出しはStorage所有の同じ領域を借りる。メンバーはPlaceLookupより前に宣言し、ファイル情報とReaderの破棄後まで寿命を維持する。

AppはSelectedDatasetの本体・参照を持たない。要求と完了通知、画面に必要な状態のスナップショットを扱う。選択中のファイル名・言語・書式・索引ヘッダーの容量、NVSに保存する設定、ファイル形式と読出し順序は変更していない。

## 検証と固定領域

AppとStorageの記録・画面遷移・GPS操作・ログ閲覧・地図と地名の借用管理を含むrun_logger_app.pyが成功した。StorageのSelectedDatasetとPlaceLookupが参照する領域のアドレス一致を確認した。cardputer-advビルドも成功した。

| 項目 | 変更前 | 変更後 |
|---|---:|---:|
| 静的RAM | 253,740 B | 253,740 B |
| Application | 217,560 B | 217,560 B |
| StorageTask | 174,224 B | 175,016 B |
| SelectedDataset | 792 B | 792 B |
| Flash | 1,241,705 B | 1,241,769 B |

ApplicationはStorageTaskを内包するため、各行を加算しない。Storageに増えた792 Bは、Appから移した同じ情報であり、全体の固定RAMは増えていない。新しいヒープ確保は追加していない。実機への書込み・速度比較は今回実施していない。

## UI表示用領域の借用可否

UIのMapFrameReceiverは240×101画素・2 bitの固定画像を2枚所有し、1枚あたり6,060 Bである。設定画面でも配列自体は保持される。未表示側には地名検索用のbeginWork／returnWorkが既にあり、画面遷移時も未返却の貸出しを破棄しない。現行DatasetViewは472 Bなので、結果の格納容量は足りる。

データセット表示の返却先にもこの領域を使う場合は、UIが貸出し、AppがStorageへ渡し、Storageの書込み完了後にAppがUIへ確定を通知する。UIが既存の画面データへ結果を取り込んでから貸出しを返す。地図生成・地名検索と同時に貸さず、世代が変わった結果は破棄しても借用は返す。UI所有の表示結果をAppのUpdateUiContentで古い値へ上書きしない更新規則も必要になる。

このデータセット表示用の借用・確定経路はまだ実装していない。現在のDatasetView返却はStorage→App→UIの値コピーを維持している。今回実装した変更はSelectedDatasetのStorage所有化である。

後続実装: データセット表示用の借用・確定経路と起動確認の4 KiB共用も実装した。現在の仕様・検証結果は[データセット表示のUI借用と初期化Reader共用](データセット表示のUI借用と初期化Reader共用.md)を参照する。
