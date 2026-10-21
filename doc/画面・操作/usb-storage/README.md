# USBストレージ

起動フラグがUSBストレージの場合に表示する専用画面。GPS・地図・辞書・ログ記録を起動せず、PC公開中はPCがSD全体を利用する。動作は[起動モード・USBストレージ仕様](../../09_起動モード・USBストレージ仕様.md)に従う。

起動時はPC・SDの状態と「ログを書き出す」「ロガーへ戻る」を表示する。上下または`;` / `.`で選び、Enterで決定する。0/1/2/3・F1・Dで通常モードの画面へは移動せず、自動消灯もしない。

「ログを書き出す」はGPX・KML・CSVへの変換を開始する。PC公開中だけ安全な取り外しを待ち、FatFsの端末操作へ切り替える。未接続・取り外し済みの場合は直接ログ選択へ進む。4件単位でログを選び、形式選択・変換・完了または失敗を表示する。出力先は`/export/<元のファイル名全体>.<形式の拡張子>`とする。画像と画面別操作は[ログエクスポート画面](export-proposal/README.md)、処理・出力の詳細は[ログエクスポート仕様](../../10_ログエクスポート仕様.md)を参照する。

「ロガーへ戻る」またはメニューでEsc / Delを押すと[共通確認ダイアログ](../共通確認ダイアログ.md)を開く。初期選択は左の取消、右が終了、左右または`,` / `/`で選択し、Enterで確定する。Esc / Delは取消とする。PC公開中でも終了を選べる。補助文「PCの保存完了後に終了」を表示し、PCでコピー・編集・保存が完了してから確定する。Windowsのクイック削除で運用する場合は安全な取り外しを必須としない。

終了を確定すると、Storageが実行中I/Oの終了を待ち、新規転送を止めてSD同期・フラグ保存・媒体の非公開化を行い、成功通知後にAppが再起動する。PCによる取り外しやケーブル抜去だけでは再起動しない。SDなし・初期化失敗でも終了操作は利用できる。

| 状態 | 表示例 |
|---|---|
| 終了確認・取消選択 | [日本語](log-browser-usb-preview.png) / [English](log-browser-en-usb-preview.png) |
| 終了選択 | [日本語](log-browser-usb-selected-preview.png) / [English](log-browser-en-usb-selected-preview.png) |
| 確認を閉じた画面 | [日本語](log-browser-usb-screen-preview.png) / [English](log-browser-en-usb-screen-preview.png) |
| PCで取り外し済み | [日本語](log-browser-usb-ejected-preview.png) / [English](log-browser-en-usb-ejected-preview.png) |
| SDエラー | [日本語](log-browser-usb-error-preview.png) / [English](log-browser-en-usb-error-preview.png) |

画像は実装のUiRendererから240×135ドットで描画し、最近傍で4倍にしたものである。`python3 tools/render_log_browser.py`で生成する。
