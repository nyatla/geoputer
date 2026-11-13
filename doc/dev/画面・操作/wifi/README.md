# Wi-Fi管理・接続画面

240×135ピクセルの実描画コードから生成した画面例。接続用QRを初期表示し、左右キーでURL用QR・文字情報へ切り替える。Rはパスワード再生成の確認、Esc／Delはモード終了の確認を開く。

| 画面 | 日本語 | English |
| --- | --- | --- |
| Wi-Fi接続用QR | [原寸](wifi-ja-connect.png) / [拡大](wifi-ja-connect-preview.png) | [Native](wifi-en-connect.png) / [Preview](wifi-en-connect-preview.png) |
| 管理画面URL用QR | [原寸](wifi-ja-web.png) / [拡大](wifi-ja-web-preview.png) | [Native](wifi-en-web.png) / [Preview](wifi-en-web-preview.png) |
| 文字情報 | [原寸](wifi-ja-text.png) / [拡大](wifi-ja-text-preview.png) | [Native](wifi-en-text.png) / [Preview](wifi-en-text-preview.png) |
| 再生成確認 | [原寸](wifi-ja-renew.png) / [拡大](wifi-ja-renew-preview.png) | [Native](wifi-en-renew.png) / [Preview](wifi-en-renew-preview.png) |

画像中の認証情報は試験用。実機では端末ごとのランダムなパスワードからQRを生成する。QR領域は100×100ピクセルで、独立した読取り器による原寸画像の復号を確認した。

生成：`python3 tools/render_wifi_setup.py --output doc/dev/画面・操作/wifi`

[Wi-Fi管理モード仕様](../../19_Wi-Fi管理モード仕様.md) / [QR接続の検証](../../検証/Wi-Fi_QR接続と認証情報再生成の検証.md)


## ブラウザーの管理画面

Web画面は「情報」「ファイル」「データセット」の3画面とし、日本語／Englishを切り替えられる。情報を初期表示とし、データセットは準備中のプレースホルダ。ファイル操作とテキスト編集はファイル画面で行う。端末本体の240×135画面はこの変更では更新しない。

画面構成・言語保存・処理中の制約は[Wi-Fi管理モード仕様](../../19_Wi-Fi管理モード仕様.md#5-web画面)、PC・スマートフォン幅の試験結果は[Web画面整理の検証](../../検証/Wi-Fi_Web3画面と日英対応の検証.md)を参照する。


| ブラウザー画面の例（模擬データ） | 画像 |
| --- | --- |
| 情報・日本語 | [表示例](web-info-ja.png) |
| 情報・英語 | [表示例](web-info-en.png) |
| データセット・準備中 | [表示例](web-datasets-ja.png) |
| テキスト編集・英語 | [表示例](web-editor-en.png) |
