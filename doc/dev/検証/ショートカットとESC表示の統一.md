# ショートカットとESC表示の統一

実施日: 2026-10-29。現行仕様は[共通キー操作](../画面・操作/共通キー操作.md)を正本とする。

## 変更

LIVEのESCで画面選択MENUを開き、それ以外のトップ画面ではESC/DelでLIVEへ戻る。子画面の戻る、編集・ダイアログの取消、BootのESCメンテナンス、消灯中の最初のキーによる復帰は維持する。LIVEのDelは何も操作しない。

すべてのフッターをUiModel::shortcutGuideとUiRenderer::presentへ集約した。並びはESCを含むグローバル、数値、ローカルの順。各キー8ピクセル送り、グループ間だけ1セル空ける。数値が使えない子画面では数字ごと省略し、Enter/Delは暗黙として表示しない。状態により使えないキーはグレーにする。Boot、USB、ダイアログも同じ描画を通し、各画面のESC DELやENTER SELECT等の重複案内を除いた。

ESCは8×8の1ビットROMパターンで直接描く。ELFのescape_icon::RowsはDROMの8バイトとして配置された。画面選択MENUは維持している。ログのMとRは物理キー配送の一覧にも加え、案内と実際の操作を一致させた。

## 資源

通常版cardputer-advの静的RAMは253,972 Bで変更前と同じ。Flash使用量は1,236,649→1,236,633 B（−16 B）。UiShortcutGuideは60 Bの局所値で、ヒープや画面ごとの固定状態を増やさない。実際のタスクスタック高水位・処理時間の再計測はしていない。

## 確認

- run_native.pyが成功。6トップ画面のESC/Del、LIVEのDel無効、数値の直接移動、子画面の数値無効、モーダル優先、復帰専用キー、メンテナンス例外を確認した。
- キー列のグループ順と空グループの省略、単一の区切り、Enter/Delの非表示、画面幅内への収まり、M表示メニューとR再集計を確認した。
- run_status_icons.pyの295描画ケースが成功。全画面・設定サブページ・USB・ダイアログ・日本語/英語・4/16ビット色で、ESCパターン、矢印、文字色、グループ空白、末尾の空白領域を画素単位で確認した。共通SD/電池ヘッダーも維持された。
- run_logger_app.pyが成功。Appとの記録操作・画面遷移・ログ閲覧の通信を確認した。
- tools/render_live_place_layout.py、render_settings_layout.py、render_navigation.py、render_log_browser.py、render_dataset_selector.py、render_dataset_warning.py、render_map_view.pyで既存の現在画面画像を再生成した。地図はN03実データの1/1・1/2・1/4を使用し、4/16ビット色の一致を確認した。
- 通常PlatformIOビルドが成功。今回の変更範囲のgit diff --checkに問題なし。

画像生成器は現在の固定出力／借用Reader APIに合わせ、利用中のPillowでも整数倍率の最近傍拡大を使えるようにした。実装画像はホストで本番描画コードを実行した結果で、実機の画面キャプチャではない。

この段階では実機への書込みとキー操作試験は実行せず、ファームウェアを.pio/build/cardputer-adv/firmware.binへ生成した。その後、[履歴再生の速度と方向操作](履歴再生の速度と方向操作.md)と合わせて実機へ書込み、通常起動を確認した。
