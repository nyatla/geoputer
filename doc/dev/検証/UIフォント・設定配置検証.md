# UIフォント・動作設定の配置検証

検証日: 2026年10月16日

## 1. 変更内容

MENU、GPS Loggerの操作・確認項目、ログ詳細の操作項目へ、Settingsと共通の言語別描画を適用した。英語はM5GFXのAsciiFont8x16、日本語は日本語表記の東雲16×16とする。MENUは日本語のGPS記録 / 設定 / ログ一覧と、英語のGPS LOGGER / SETTINGS / LOGSを切り替える。ログ詳細は測位点を見る / 再読込とVIEW POINTS / RELOADを切り替える。GPS Loggerの開始・再開・停止・保存中・取消・実行も同じ規則とする。

フォントの役割名をSettingsEnglishからAscii8x16へ改め、画面に依存せず共通利用できるようにした。地名とLive Placeの主数値は東雲16×16、画面タイトル・ログのファイル名とデータ・キーガイド・ライセンスは既存の8×8を使用する。

動作設定の値は従来のx=164（消灯時間はx=156）への左詰めから、実描画幅を引いて右端x=236へ揃える方式へ変更した。項目名の左端は全行x=20とする。文字幅は英語ASCIIが8、日本語が16ドット。言語選択の値は現在の表示言語にかかわらず、日本語を16×16、Englishを8×16とする。

## 2. ホストでの配置・字形検証

`python3 tools/render_settings_layout.py --license-baseline .pio/license-multilang-before.bin`が成功した。日本語・英語それぞれの全7項目・全選択肢を実装のUiRendererで描画し、項目名の左端と値の右端を元のビットマップ字形と画素比較した。項目と値の間隔が8ドット以上あり、右余白を越えないことも確認した。言語名切替、100%、消灯しない / Never、単位付きの秒数、読取専用項目を含む。日英各11状態の画像を更新した。

ライセンス画面は先頭・途中・末尾のフレームを比較し、言語間・変更前の保存済みフレームとの完全一致を確認した。

`python3 tools/render_log_browser.py`が成功した。日英各15状態を実装の描画から生成し、MENU・ログ詳細・GPS Loggerの文字が元のAsciiFont8x16 / 東雲16×16の画素と一致すること、右余白・ヘッダー・フッターを越えないことを確認した。日本語の既存画像は更新し、英語の画像をen付きで追加した。日英のMENU・動作設定と英語のログ詳細のプレビューも目視確認した。

`python3 tests/run_native.py`も成功した。既存のキュー・同期要求、UI操作、設定・言語、GPS、ログ保存・読出し、フォント、ライセンス等の試験を通過した。

## 3. ビルドと実機

通常ファームのPlatformIOビルドが成功した。警告・エラーはなく、静的RAMは59,040 / 327,680バイト、Flashは1,072,997 / 3,342,336バイト。生成物のSHA-256は`ac4bc6cfb31bd55938520552dfd259bc8cff94133bfb2f88fc5e17f13500e8de`。

COM6のMAC `70:af:09:df:46:d0`を照合し、上記の通常ファームを0x10000へ書き込んだ。書込み後のハッシュ照合は成功した。30秒の起動ログで240×135、4タスク・5キュー、NVS設定LOADED、SD FAT32、GPS UART、Galuchat OPEN OK、ReadyからLivePlaceへの遷移を確認した。GPSの有効Fix、JST補正、地名取得、矩形キャッシュとWordBookのMISS→HITも確認した。観測中にpanic・assert・再起動はなかった。ログは`.pio/font-layout-boot.log`へ保存した。各画面を実キーで開いた見え方は、ホストでの画素比較と区別する。

## 4. 関連する画面仕様

- [共通の画面・操作機能設計](../画面・操作/02_画面・操作機能設計.md)
- [Settings](../画面・操作/settings/README.md)
- [MENU・Logs](../画面・操作/logs/README.md)
- [ログ詳細](../画面・操作/log-detail/README.md)
- [GPS Logger](../画面・操作/gps-logger/README.md)
