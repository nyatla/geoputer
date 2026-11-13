# Wi-Fi QR接続と認証情報再生成の検証

2026-10-30。Wi-Fi接続用QR・管理画面URL用QR・文字情報の切替、パスワード再生成、DNS／HTTPによる接続案内を追加した。実機への書込み、スマートフォン実機によるQR読取り・無線接続、OSごとの案内の自動表示はまだ確認していない。

## 実装

- 左右で接続用QR・URL用QR・文字情報を循環する。初期は接続用QR。最初の接続を検知した場合、接続用QRからURL用QRへ進む。
- Rで再生成確認を開く。取消が初期選択。確定後に新しいランダムな16文字を保存し、Wi-Fiモードのまま再起動する。保存失敗時は現在の認証情報を維持する。
- 起動ごとのパスワード変更はしない。既存WAP1レコードを読み続け、NVSの読出し失敗と破損を区別する。乱数は無線を有効にした後のesp_randomを使用する。
- 接続後のDNS・HTTP案内を追加した。案内が自動表示されない場合はURL用QRで開ける。ファイル操作の画面には通常ブラウザー用のリンク・URLを案内する。
- QRのビット行列と内容キャッシュはUIタスクが所有し、Rendererへ読み取り専用で貸す。新しい画面バッファやUI内のヒープ確保は追加しない。QRエンコードが終わってから画面を描くため、大きなフォント描画のスタックと同時に積まない。
- 既存M5GFX内のQRエンコーダを使用し、上流のMITライセンス表示を追加した。

## 試験

| 試験 | 確認範囲 |
| --- | --- |
| tests/run_wifi_setup.py | 乱数パスワードの保存・再読出し・再生成・保存失敗・読出し失敗・同一値の再生成拒否。実際のM5GFX QRエンコーダを使用し、固定バージョン・白余白・エスケープ・欠損情報を確認 |
| 独立したZXing-C++復号 | 接続用・URL用の100×100原寸画像を復号し、SSID・パスワード・URLと一致することを確認 |
| tools/render_wifi_setup.py | 日本語・英語の接続用QR・URL用QR・文字情報・再生成確認を、実UiRendererで240×135へ描画。実画面のQR部分を原寸で切り出しても復号が成功 |
| tests/run_native.py | 左右循環、接続検知によるページ切替、手動ページの維持、Rの確認・取消・確定、再起動中の重複操作拒否。既存の設定・GPS・USB・アラート等の回帰試験 |
| tests/run_logger_app.py | GPS・ログ・地図・USBの実Application回帰試験 |
| tests/wifi_browser_tests.cjs | 320・390・768・1280pxの既存ファイル管理。320・390pxの接続案内、URL表示、管理画面へのリンク。API応答は試験用であり無線接続は代替しない |
| PlatformIO cardputer-adv | ESP32-S3向けのビルド成功 |

ASan／UBSanを使用した。実行環境の制約によりLeakSanitizerは無効にし、リーク検出の成功とは区別する。ZXing-C++とPillowは一時環境に導入した試験用ツールであり、ファームウェアの依存ではない。

## 再実行

```sh
ASAN_OPTIONS=detect_leaks=0 python3 tests/run_native.py
python3 tests/run_wifi_setup.py
python3 tools/render_wifi_setup.py --output /tmp/wifi-preview
RGP_PLAYWRIGHT_MODULE=/path/to/node_modules/playwright node tests/wifi_browser_tests.cjs
```

tests/run_wifi_setup.pyはZXing-C++とPillowが利用できる場合、独立した復号も実行する。通常のホスト環境には追加インストールを必須としない。

## 実機での確認項目

Wi-Fi起動時に接続用QRを表示できること、スマートフォンで接続後にURL用QRへ移ること、案内ページまたはURL用QRから管理画面へ入れることを確認する。Rの取消では接続が維持され、確定時は再起動後の新しいQRで接続できることを確認する。旧パスワードの保存状態や案内の自動表示はOSごとの差を確認する。

## ビルドとメモリ

比較元は[Wi-Fi管理モード初期実装](Wi-Fi管理モードの検証.md)の最終ビルドである。

| 項目 | QR追加前 | 追加後 | 差 |
| --- | ---: | ---: | ---: |
| 固定RAM | 281,860 B | 282,188 B | +328 B |
| Flash | 1,775,669 B | 1,791,685 B | +16,016 B |

ESP32-S3のELF型情報ではQrCodeが270 B、WifiServiceが6,264 B、Wi-Fi一式が6,888 B。Wi-Fi一式は既存のGPS一式より小さく、最上位unionの予約を拡大しない。QRキャッシュはUIループのスタックで保持する。UIタスクのスタック予約8,192 Bは変更せず、UIループ本体の固定フレームは2,960 Bである。呼出先とQRエンコーダの局所スタックを含む実機の最大使用量は、まだ計測していない。

最後のビルドに警告・コンパイルエラーはない。ファームウェアは `.pio/build/cardputer-adv/firmware.bin`。

SHA-256：`51a7572b7a8dd76b3091f352b762db9255613c110895c108b5a0d7515e2e599a`。実機は未更新である。
