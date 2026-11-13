# GLC 4 KiB共通借用領域の検証

実施日: 2026-10-27。GLC読出しの先読みを16 KiBから4 KiBへ変更し、UIが貸す同じ4 KiBを索引とGLCのReaderが順に使う方式を実装した。採用容量の根拠は[現行4/8/16 KiB比較](現行GLC読出しバッファ比較.md)。今回の実装ではGaluchat Core・SDKを変更していない。

## 所有権と処理順序

UIの固定back画像6,060 Bのうち、先頭4,096 Bを借りる。AppがUIから取得した貸出しID・アドレスをStorageへ渡す契約は維持する。地図生成ではMapBuildJob::readBufferが同じBorrowedReadBufferをMapSourceへ渡し、索引とGLCの両Factoryへ指定する。索引の収集を完了してReaderを閉じてからGLCを開くため、同じ領域を二重に取得しない。

地名検索でも先頭4 KiBを両Factoryへ結び付ける。GLC FactoryをPlaceLookupに保持する追加のshared_ptrを設け、検索のスコープ内だけ借用先を設定する。両Factoryは早期return・例外を含めて設定を解除する。PlaceResultは従来どおり4 KiB以降の整列済み領域へ置き、GLCの読出しでは触れない。

GLCの後にWordbook検索を行う。Wordbook索引は同じUI領域を借りるが、本体Readerは索引と同時に生存するため、別の4 KiBを自己所有する。GLC・索引のReader本体、FIL、Factory、辞書の数値配列・文字列、起動確認・索引保守の別Reader等のヒープは残る。

通常AppのGLC用16 KiB動的バッファはなくなる。UIに新しい配列は追加しない。借用指定なしの独立した呼出しは、同じ4 KiBを自己所有する互換経路を維持する。Config.hでMapRectReadBufferBytes=4096とし、IndexReadBufferBytes以内に収まることをstatic_assertで確認する。

## 通常ファーム

| 項目 | 変更前 | 変更後 |
|---|---:|---:|
| 静的RAM | 259,360 B | 259,368 B |
| Flash | 1,235,653 B | 1,235,905 B |
| UIの固定境界画像 | 6,060 B×2 | 同じ |
| 地名判定用／地図用の固定区域コード画像 | 64 KiB＋96 KiB | 同じ |
| 通常AppのGLCバッファの動的確保 | 16,384 B／Reader | 0 B |

静的RAMの+8 Bは既存GLC Factoryへのshared_ptrを保持する参照の増分。Factoryをもう1個作ったものではない。GLC16 KiBの除去はバッファ本体の確保量であり、システム全体の最低空き量が必ず16 KiB増えるという意味ではない。

## ホスト試験

- run_borrowed_reader.pyでstdio／FatFs模擬×診断ON/OFFをASan/UBSan検証。異なるFactoryが同じ領域を索引→GLC→索引の順に借りられること、重なったReaderの取得を拒否すること、EOF・ビット処理・容量超過・例外時の返却を確認。
- run_borrowed_map_files.pyでN03・estatの各1/1・1/2・1/4を比較。自己所有4 KiB版と共通借用版の全区域コード・全境界バイトが一致し、各要求で4 KiBの確保を2回、計8,192 B省いた。これは新しい4 KiB版同士の累積要求量の比較であり、前の16 KiB版とのピーク比較ではない。
- run_borrowed_place_files.pyで両データセットの7問い合わせ、区域コード・地名・要求ID・世代の一致を確認。N03の累積要求確保量57,963→14,795 B、estatは98,062→44,814 B。GLC再取得とWordbook索引の双方が影響するため、1問い合わせのピーク量とは区別する。
- GLCを短い別ファイルへ差し替える失敗試験、索引切断、検索後の通常自己所有検索、再オープンを確認。元データセットへのシンボリックリンクは一時的に退避し、元GLCは変更していない。
- Storage作業域のclose失敗・取消・世代違い・100回の排他切替、実Appの拡大縮小取消・古い完了・UI貸出し・GPS更新・atomic commitも成功。

LeakSanitizerがptrace環境で実行できないため、ASan/UBSanはdetect_leaks=0とし、Readerの使用中フラグ・明示的な解放・失敗後の再借用を検査した。地図ジョブの単体試験で不足していた有効なUIバッファも試験用データへ追加した。

## 実機確認

Cardputer ADV、既存FAT32・dataset/02、SPI要求25 MHz／実20 MHz。製品とは分離した計測版で、仮想現在地・通常地図・区域強調変更・範囲外移動と帰還・1/2・1/4・ログ地図・文字表示への復帰を2起動で自動実行した。記録開始やNVS設定保存は行わない。

ObservedFileReaderの構築完了時に、パス・実バッファ容量・借用の有無・空きヒープ・スタック残量を記録した。各起動ともGLC Reader12個、索引Reader14個がすべてUIからの借用で、容量は4 KiB以下。Wordbook本体Reader14個は従来どおり自己所有だった。地図の実読出し7回／起動を含め、両起動でSESSION-BENCH-DONE OKとなった。

各起動のGLC Reader活動時に観測した最小空きヒープは58,676 B。これは構築完了時のサンプルであり、全処理のピークやシステム全体の最低値ではない。Storageの観測最低スタック残量は2,988 B。パニック・スタック保護違反・再起動なし。Reader構築ログの負荷があるため、この計測版の時間を前回の容量別ベンチマークへ直接比較しない。

## 保存と復帰

[実機Reader記録CSV](GLC4KiB共通借用領域の実機記録.csv)。`.pio/glc-buffer-borrow-checks/`に変更前ソースとファーム、変更後ソース・通常版、分離計測版・生ログ、ホスト試験、ビルド・書込み結果、summary.jsonを保存した。

確認後、GLC4 KiB・UI借用を有効にした通常ファームへ戻して起動、FAT32、GPS FIX、Galuchat OPEN、習志野市の地名表示を確認した。通常版SHA-256: `d566e2040a30c68adee86816e4b98584faff61db3ac1adc1e1449356b395779a`。診断の固定座標・自動操作・Readerログ・キー抑止は通常版に含めない。
