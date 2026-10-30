# Wi-Fi管理モードの検証

2026-10-30。基本ファイル管理を実装し、ESP32-S3ビルドとホスト・ブラウザー試験を実施した。実機への書込み、スマートフォンと実機間の電波接続、実SDカード上での転送速度・実行時ヒープはまだ確認していない。

## 実装範囲

システム設定のモードにWi-Fiを追加し、GPS・USBと排他的に起動する。パスワード付きSoftAPの接続情報を本体へ表示し、ブラウザーからSD内のフォルダー移動・作成・再帰削除、単体ファイルのアップロード・削除、UTF-8テキストの表示・編集を行う。テキスト上限は64 KiB、単体アップロード上限は2,147,483,647バイト。tgzの展開とWebからの索引保守は後続機能である。

WebサービスはStorageが所有し、HTTPとファイルI/Oを同じStorageループで直列処理する。UIとAppは別に稼働し、本体からの終了要求は受信・削除処理中にも既存キューを通して取り込む。HTTP用のアプリケーションタスクとキューは追加しない。

## 試験

| 試験 | 結果と範囲 |
| --- | --- |
| tests/run_wifi.py | 実際のSD専用FatFsを64 MiBのメモリ媒体でFAT32として実行。UTF-8ファイル名、分割したバイナリ書込み、既存ファイルの保護、古い編集内容の拒否、途中中断、同期失敗、置換処理の途中での再起動、再帰削除と取消、管理領域の短縮名保護を確認 |
| tests/run_native.py | 既存のキュー・UI・設定・GPS・ログ・USB・INI・アラート・音声の回帰試験に加え、Wi-Fiのモード候補、確認ダイアログ、Esc／Del、数字・GPS・消灯キーの抑止、NVSの3モード保存を確認 |
| tests/run_mode_resources.py | Wi-Fiの最上位unionへの選択、他モードへの再構築拒否、既存Storageキュー経由の終了要求を確認。ホストでは無線ハードウェアを起動しない |
| tests/run_logger_app.py | 既存の実ApplicationによるGPS記録・一時停止・停止・地名・地図・ログ・USBの回帰試験が成功 |
| tests/wifi_browser_tests.cjs | Chromiumの320・390・768・1280px幅で横はみ出しなし。ページ送り、テキスト編集・保存・未保存取消、フォルダー作成と移動、単体アップロード・上書き確認、削除取消、状態表示、終了操作を確認。APIは試験用の応答を使用 |
| PlatformIO cardputer-adv | ESP32-S3向けのコンパイルとリンクが成功 |

ホストのASan／UBSanを使用した。実行環境のLeakSanitizer制約によりASAN_OPTIONS=detect_leaks=0を指定し、リーク検出の成功とは区別する。ブラウザー試験は画面とHTTP要求内容の確認であり、実無線や実機HTTP処理全体の動作確認を代替しない。

## 再実行

```sh
ASAN_OPTIONS=detect_leaks=0 python3 tests/run_native.py
ASAN_OPTIONS=detect_leaks=0 python3 tests/run_logger_app.py
python3 tests/run_wifi.py
# PlaywrightとChromiumを用意した試験用Node環境から実行する。
RGP_PLAYWRIGHT_MODULE=/path/to/node_modules/playwright node tests/wifi_browser_tests.cjs
```

Playwrightは開発用の外部試験ツールで、ファームウェアの依存には追加していない。ブラウザー試験のスクリーンショットは一時ディレクトリへ保存する。

## メモリ

Wi-Fi用の固定作業は最上位のApplicationModeMemoryへ排他配置する。ESP32-S3のELF型情報ではFilesが4,936 B、WifiServiceが6,160 B、Wi-Fi一式が6,784 B、モードunionが175,744 Bである。Web画面のHTML・CSS・JavaScriptはFlashの読み取り専用領域に配置される。

Wi-Fi用の固定作業がGPS一式の予約内に収まることと、無線ライブラリの共通領域が増えることは別である。Wi-Fi／TCP-IP／HTTPライブラリの内部確保は残り、GPS・USB起動でもリンクされた共通固定領域は予約される。実行時の最小空きヒープとスタック余裕は、実機接続試験で測定する。

## 実機での次の確認

本体で記録停止後、設定→システム設定→モードをWi-Fi→保存して再起動。本体に表示されたSSIDとパスワードでスマートフォンを接続し、表示URLを開く。インターネットなしの警告が出てもこのWi-Fi接続を維持する。

実SDで日本語INIとGLCを転送し、テキスト保存、上書き、切断、中断、SDなし、容量不足、本体とWebからの終了を確認する。続いてGPS・USBへの復帰とメモリを確認する。

## 最終ビルドの比較

比較元は直前の[動作モード別unionの検証](動作モード別unionの検証.md)に記録された、Wi-Fi追加前のファームウェアである。

| 項目 | Wi-Fi追加前 | 追加後 | 差 |
| --- | ---: | ---: | ---: |
| 固定RAM | 254,612 B | 281,860 B | +27,248 B（約26.6 KiB） |
| Flash | 1,284,093 B | 1,775,669 B | +491,576 B |

固定RAMはリンカ集計であり、実行時の空きヒープではない。Wi-Fi専用作業の6,784 Bは既存の大きいGPS資源とunionで重なるが、無線ライブラリの共通領域と共通通知データの拡張は別に増える。GPS・USBの起動中にもこの共通予約は残る。

最終ビルドにコンパイルエラー・警告はない。ビルド成果物は `.pio/build/cardputer-adv/firmware.bin`。

ファームウェアSHA-256：`c7b4622fa25ee95957dac446f9e648928227280e30a30138bcea1ec5b9756360`。
