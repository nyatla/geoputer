# Geoputer Boot画面

文書状態: Draft 0.8  
対象画面: 240 × 135 px、横向き  
入力: Cardputer ADV 56キーキーボード  

[画面・操作機能設計（共通仕様・画面一覧）](../02_画面・操作機能設計.md)

## 1. 目的と表示

起動時に各機能の準備状況を表示し、利用できない機能と理由を確認できるようにする。現在の実装段階では6項目を表示する。

```text
GEOPUTER            STARTUP
------------------------------
[+] DISPLAY        240X135
[+] TASKS/QUEUES    4T/5Q
[+] NVS SETTINGS    LOADED
[!] SD CARD         NO CARD
[+] GPS UART        UART OK
[!] GALUCHAT DATA   NO SD
GPS:MESH  DATASET:01
------------------------------
WITH WARNINGS     ENTER START
```

文字はタイトルを含め8×8ドット、アンチエイリアスなしとする。ヘッダー下の罫線はy=22、チェック行はy=29から14px間隔の6行、GPS接続・データセット番号はy=110、フッター上の罫線はy=118とする。項目名と結果は別の列に配置し、結果の略称は9文字までとする。

| 項目 | 確認内容 | 主な結果 |
|---|---|---|
| DISPLAY | UIの240×135描画領域とフォントの準備 | 240X135 |
| TASKS/QUEUES | App・UI・GPS・Storageの4タスクと5キューの準備 | 4T/5Q、TASK ERR |
| NVS SETTINGS | 保存領域の準備、設定レコードの読込み・形式・検査値 | LOADED、DEFAULT、INVALID、NVS ERROR |
| SD CARD | カードの認識、マウントとFAT32判定 | FAT32、NO CARD、NOT FAT32、SD ERROR |
| GPS UART | Mesh KitのUART受信準備 | UART OK、UART ERR |
| GALUCHAT DATA | 番号01のファイル検出、Reader生成、ヘッダー・辞書形式・文字コード確認 | OPEN OK、NO SD、MISSING、MULTIPLE、ENCODING、DATA ERR |

Galuchatの確認はGPS Fixを要求しない。データをRAMへ全展開せず、検索用のReaderを準備する。OPEN OKは起動時に必要な読出し準備の成功を表し、地図・辞書の全領域の走査や全地名の復号を完了した意味ではない。実検索中の読込異常はLive Placeへ別途表示する。

GPS UARTの成功は受信設定の準備までであり、GPS機器からのNMEA受信や測位成功を意味しない。GPS MODEはチェック行にせず、現段階の固定値MESHを補助表示する。GNSS PAUSEは未実装のため表示しない。GPSログはSTOPPEDのまま開始する。

## 2. 状態と色

| 表示 | 状態 | 色 |
|---|---|---|
| [.] | 未確認 | 暗灰 |
| [~] | 確認中 | シアン |
| [+] | 準備成功 | 緑 |
| [!] | 機能制限・確認を実施できない | 黄 |
| [X] | 準備・検証失敗 | 赤 |

NVS設定領域とSDの準備結果は分離する。SD未挿入・マウント失敗でもNVSの設定読込みを行う。設定が未保存の場合は初期値を使用し、DEFAULTとして正常表示する。設定が不正・読込不能ならエラーを表示し、初期値で継続する。SDが使用できない場合、GALUCHAT DATAはNO SDとし、未確認のデータを成功表示しない。

## 3. 完了と操作

確認中はCHECKING...を表示し、画面遷移・設定操作・無操作消灯を行わない。すべての確認結果が揃った時点でREADY、警告または失敗があればWITH WARNINGSへ切り替える。

完了後は通常2秒、警告・失敗がある場合は5秒表示し、Live Placeへ自動遷移する。Enterは完了後だけLive Placeへの遷移要求として受け付ける。2・Esc・Del・上下左右ではBootを抜けない。Live Placeの無操作期限はBootを抜けた後から数える。

SD・NVS・GPS・Galuchatの障害は非致命的とし、利用可能な機能で起動を継続する。画面やUIの準備に失敗してチェック画面そのものを表示できない場合は、シリアルログへ初期化失敗を出して停止する。

## 4. タスク連携

Appが確認結果を集約し、SetUiScreen / UpdateUiContentでBootの値コピーをUIへ渡す。UIは描画とEnter操作を担当し、初期化・SD・NVS・Galuchat読出しを直接実行しない。

NVS読込みはLoadSettings、Galuchat読出し準備はCheckDatasetの通知でStorageへ要求し、ReadCompleted / ReadFailedで結果を受け取る。未投入の要求だけを再試行し、投入済み要求を重複実行しない。GPS受信はBootの表示中も継続する。
