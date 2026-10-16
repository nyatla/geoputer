# Geoputer GPS Logger画面

文書状態: Draft 0.11（記録操作・保存実装済み、実GPSでの操作通し確認は未完了）  
対象画面: 240×135 px、横向き  
入力: Cardputer ADV 56キーキーボード  

[共通仕様・画面一覧](../02_画面・操作機能設計.md)

## 1. 目的

GPSログの開始・再開・停止と、記録・保存状態の確認を行う。緯度・経度・速度・進行方位・海抜高度・衛星数等のGPS情報は[Live Place](../live-place/README.md)へ集約する。本画面ではGaluchat検索を実行しない。

## 2. レイアウト

```text
┌──────────────────────────────┐
│GPS LOGGER          REC       │
│                              │
│LOG 03:42:18                  │
│SAVED 13336    UNSAVED 2       │
│BUF 2/64 LOST 0               │
│LAST SAVE 2S                  │
│                              │
│> STOP                        │
│                              │
│Enter OK          Esc/Del Back│
└──────────────────────────────┘
```

起動時の記録状態はSTOPPED、保存・未保存・欠落件数は初期値とし、操作項目をSTARTとする。状態の画面表記はSTOP / WAIT / REC / PAUSEとする。STARTING中のWAITは新しい有効Fix待ちを示す。終了処理中はCLOSINGとSAVING...を表示し、新たな開始を受け付けない。PAUSEDではRESUMEとSTOPを選べる。現在は記録状態にかかわらずGPSのUART受信を継続する。

## 3. 表示項目

- ログ状態と開始・再開・停止の操作結果
- 一時停止時間を除く記録時間
- SDへ保存した件数、未保存件数
- RAMバッファ使用数/64
- 実際に失った測位サンプルの欠落件数
- SD最終保存時刻または経過秒
- Storageの受付・保存エラー種別

RAMへの追加受付エラーはtryAppendLogの通常の戻り値、開始・flush・停止の受付エラーは同期メッセージの戻り値、受付後の保存エラーはStorageError通知でAppが把握し、UpdateUiContentでUIへ渡す。SD ERRORやBUFFER FULLと、未保存・欠落件数を確認できるようにする。受付失敗だけで欠落件数を増やさない。Appが回復を確認するまでエラーを保持し、GPS更新やログ停止だけで消さない。

保存済み件数は実書込み・flush成功を確認した件数とし、SD青の点灯を保存成功の代わりにしない。SD表示は地図・辞書を含む実I/Oと共通の観測状態に従う。

## 4. GPSログ操作

上下キーで操作項目を選択し、Enterで決定する。UIは選択・確認操作をタスク内で処理し、確定操作を保持して内容更新を通知する。Appが同期取得してログ操作を実行し、処理結果をUIへ反映する。Escはキャンセル・Live Placeへ戻る。左右キーは使用しない。

| ログ状態 | 選択可能な操作 |
|---|---|
| STOPPED | START |
| STARTING | STOP（開始待ちを中止） |
| RECORDING | STOP |
| PAUSED | RESUME、STOP |

- STARTは新規セッションを準備し、STARTINGを経て次の新しい有効Fixから記録を開始する。操作前の古いFixは保存しない。
- RESUMEは一時停止中に取得した点を遡って記録せず、次の新しい有効FixからRECORDINGへ戻す。ログとセッションを継続する。
- STOPは確認ダイアログを開く。Enterで停止、Escでキャンセルする。
- 停止確定時は新しいログ追加を止め、保存・終了要求の受付結果を同期操作で取得してSTOPPEDへ移行する。実保存とファイル終了はStorageが非同期に処理して通知する。
- flushに失敗した未保存レコードはRAMに保持し、件数とSDエラーを表示する。ログ状態と保存エラーは別に表示する。
- 記録の一時停止はLive PlaceのF1から行う。一時停止・停止でGPSデバイスをOFFにせず、UART受信とLatestFix更新を継続する。GNSSの休止・復帰は後続実装とする。

保存エラーは上記の記録状態とは別に保持する。現行のRecordingStateにSTORAGE_ERRORは設けず、LogErrorと保存進捗を表示する。Appの記録要求を保存障害だけで解除せず、復旧後の受付・保存を再試行する。

```text
┌──────────────────────────────┐
│GPS LOGGER          REC       │
│LOG 03:42:18                  │
│SAVED 13336    UNSAVED 3       │
│BUF 3/64 LOST 0               │
│STOP LOG?                     │
│> CANCEL                      │
│  OK                          │
│Enter OK     Esc/Del Cancel    │
└──────────────────────────────┘
```

通常表示にも操作欄を設け、RECORDINGではSTOP、PAUSEDではRESUMEとSTOPを表示する。Live PlaceのGPS情報はログ一時停止を理由に消さず、そのGPS項目の有効性に従って表示する。

## 5. 現行実装

本画面の操作とログ保存を実装した。ヘッダーにGPS LOGGERと実際の記録状態を置き、本文に記録時間、保存件数、未保存件数（Appが保持する1点を含む）、64件バッファ使用数、欠落件数、最終保存からの経過秒を8ドットで表示する。下段のSTART / RESUME / STOPは東雲16ドットとし、停止は取消を初期選択した確認を経る。画面消灯や画面切替で記録を止めない。

現在はUART受信を継続する座標受信方式であり、GNSS PAUSEへの切替は未実装とする。ログ一覧・読出しは[Logs](../logs/README.md)と[詳細・測位点](../log-detail/README.md)で実装済みとし、閲覧中も記録を継続する。保存失敗時はレコードをRAMに保持し、5秒間隔で同じ書込み位置へ再試行する。Appに未受付点が残る間の一時停止・停止は、境界までの受付を待つため操作待ち表示となる。有効Fix待ちの記録状態STARTINGとは区別する。

実機SDへの書込み・CRC読戻しは固定入力による単体試験で確認済み。通常ファームウェアの実GPSとキー操作を組み合わせた開始→一時停止→再開→停止の通し確認は未完了である。[メイン画面・ログ保存検証](../../検証/メイン画面・ログ保存検証.md)を参照する。


## 操作項目の言語とフォント

開始・再開・停止・保存中および停止確認の項目は、表示言語に従う。日本語は開始 / 再開 / 停止 / 保存中 / 取消 / 実行を東雲16×16、英語はSTART / RESUME / STOP / SAVING... / CANCEL / OKをAsciiFont8x16で表示する。タイトル、保存件数・状態・キーガイドは既存のASCII 8×8とする。

[日本語の停止操作](log-browser-logger-preview.png) / [English](log-browser-en-logger-preview.png) / [日本語の再開操作](log-browser-logger-paused-preview.png) / [English](log-browser-en-logger-paused-preview.png)
