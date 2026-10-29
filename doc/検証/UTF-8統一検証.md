# GeoPuter UTF-8統一検証

2026-10-29追記: 以下は導入時点の検証記録。現在はGW00の任意メタデータによる文字コード判定を廃止し、実際に読む地名のUTF-8検証だけを維持する。現行仕様は[データセットメタデータ仕様6節](../08_データセットメタデータ仕様.md#6-glcwordbookとの役割分担と検証)を参照する。

更新日: 2026-10-17  
対象: Wordbook入力、地名の整形・キャッシュ、フォント生成、ファームウェア

## 1. 実装と利用条件

Wordbook、dataset.ini、タスク間メッセージ、UIの文字列をUTF-8に統一した。StorageのCP932からUTF-8への変換関数、約22KiBの変換表、変換表の生成スクリプトを削除した。Wordbook内部のtoken_encodingを優先し、未指定または空の場合はtext_encodingを使う。大文字・小文字を区別せずutf8またはutf-8だけを受け付け、その他や未指定はEncodingErrorとする。不正なUTF-8の地名は検索時に拒否し、地名キャッシュへ登録しない。

SDカード上でSJIS版を使用していた場合は、同じGLCに対応するUTF-8版Wordbookを配置し、dataset.iniのwordbookをそのファイル名へ変更する。配置例はN03-20260101.giswordbookを指定する。拡張子の前にutf8を含むことは条件にせず、ファイル内部の宣言と実際のバイト列で検証する。GLCとNVS設定レコードの形式は変更していない。

東雲の生成ツールは、元のJISフォントの文字番号をビルド時にUnicodeへ対応させる。互換記号と半角カナの対応をUnicodeの定数で記述し、CP932・Shift_JISのコーデック依存を除いた。生成後のShinonomeData.incのSHA-256は変更前と一致し、字形の変更はない。原典のフォントソースとライセンスは保持している。

過去の検証文書にあるSJIS辞書・変換処理の記載は、その検証時点の記録であり、現行の入力仕様ではない。現行の仕様は[データセットメタデータ仕様](../08_データセットメタデータ仕様.md)に従う。

## 2. ホスト試験

`python3 tests/run_native.py`で、UTF-8地名と互換漢字・半角カナ・ASCII・4バイト文字の検証、各階層での不正UTF-8・NUL・旧文字コードのバイト列の拒否、既存のUI・設定・GPS・同期通信・ログ・フォントの試験が成功した。

実ファイル試験は次のコマンドで実施した。データ本体への変更は一時ディレクトリのコピーだけに行い、元の配布データは変更していない。

```sh
python3 tests/run_place_files.py \
  ../dataset/maps/mlit_go_jp.N03-20260101/build/N03-20260101-grid-4096-1000.remap.wgsmapset.glc \
  ../dataset/maps/mlit_go_jp.N03-20260101/build/giswordbook/N03-20260101.giswordbook \
  --unsupported-wordbook ../dataset/maps/mlit_go_jp.N03-20260101/build/giswordbook/N03-20260101.sjis.giswordbook
```

| 確認内容 | 結果 |
|---|---|
| UTF-8実辞書による習志野市庁舎・千代田区の検索 | 成功 |
| 実GLCの324点でキャッシュと点検索が一致 | 成功 |
| utf8 / utf-8、大文字表記、text_encodingへの代替 | 成功 |
| token_encodingを優先し、cp932 / sjis / shift_jis / shift-jis / utf16 / 未指定を拒否 | 成功 |
| 実際のSJIS版WordbookをEncodingErrorとして拒否 | 成功 |
| 不正UTF-8のトークンをキャッシュせず再試行し、辞書修復後に復旧 | 成功 |
| 未対応文字コードの切替候補を拒否し、使用中のUTF-8キャッシュを維持 | 成功 |
| キャッシュヒット時のSDアクセスなし、既存の欠損・破損・復旧処理 | 成功 |

## 3. ファームウェア

PlatformIOのcardputer-advビルドに成功した。リンク後のELFにCP932の変換関数・変換表のシンボルがないことを確認した。

| 項目 | 結果 |
|---|---:|
| 静的RAM | 53,376 / 327,680 B |
| Flash | 1,071,049 / 3,342,336 B |
| 直前のデータセット切替版からの静的RAM削減 | 8 B |
| 同Flash削減 | 23,432 B |

ファームウェアSHA-256: `43aa13b6a5f162775ace498aac6e64884fc696d6afd772a91d5bf427eb93f1ae`

検証用バイナリは`.pio/utf8-only-firmware.bin`、ビルドと試験のログは`.pio/utf8-only-*.log`に保持する。上記の容量差はリンク時の比較であり、実行時の空きヒープ増加を示す値ではない。

## 4. 実機書込みと起動確認

2026-10-17にCOM6のCardputer ADV（MAC `70:af:09:df:46:d0`）へ、上記SHA-256のファームをアプリ領域0x10000から書き込んだ。esptoolの書込み後照合は成功した。NVS・SDの内容は変更していない。

通常起動後にUI・GPS・Storageの起動、NVS読込み、SDのFAT32マウントとGPSのFIXを確認した。ただし、選択先は`DATASET 01 MISSING`となり、仕様どおりBootのデータセットエラー表示で停止している。INIまたは指定GLC・Wordbook・言語の欠落はこのログだけでは区別できない。SD上の`/dataset/01/dataset.ini`と指定ファイルの配置を確認し、UTF-8版Wordbookへ参照を合わせた後に再起動する必要がある。実機での地名取得・データセット切替操作の検証は未完了である。

書込みログは`.pio/utf8-only-flash.log`、起動ログは`.pio/utf8-only-boot.log`に保持する。

その後、SD上のINIのファイル名指定を合わせ、[メニュー遷移修正版の実機試験](メニュー遷移リセット修正検証.md)でデータセット01のOPEN OKとUTF-8地名取得を確認した。上記のMISSINGは最初の書込み時点の記録である。
