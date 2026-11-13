# 地名検索Factory撤去とReaderのスコープ化

## 所有関係

地名検索の地図・索引・Wordbook用Factoryと共有所有を廃止した。StorageのPlaceLookupが、ファイルサイズ・読出し設定・安定したパスへの参照を持つFileReadSourceを直接保持する。CachedMapReader、DiskIndexedMapReader、DiskIndexedWordBookReaderは入力情報を借用し、所有しない。

ReaderはwithRangeの実行中だけスタック上に構築し、完了・例外のどちらでも固定FILと読出しバッファを返す。借用バッファは検索１回の引数として渡し、永続するファイル情報には保存しない。地図表示も同じスコープ付き読出しを使う。SDKとcoreのソースは変更していない。SDKのReaderFactory APIを使うメタデータ確認等には互換アダプタを残す。

地名検索時もApp経由でUIから借りた4KiBを使用し、索引・GLC・WordbookのReaderを順に開閉する。Storageの128×128区域コードキャッシュ、地図表示用区域コード画像、UIの借用作業領域は別の資源である。今回スタックへ移したのはReader本体であり、4KiBのバッファではない。

## ESP32向けビルドのメモリ

通常ビルドの保存済み比較元ELFと変更後ELFから確認した。Factoryの値はmalloc管理領域を含まない。

| 項目 | 変更前 | 変更後 |
|---|---:|---:|
| 地名検索のFactory本体＋参照カウント（ヒープ） | 84 B × 3 = 252 B | 0 B |
| PlaceLookup本体 | 2,808 B | 2,928 B |
| 通常ファームの静的RAM | 253,284 B | 253,404 B |

FileReadSourceは40 B、optionalを含め１枠44 B。３枠を所有者へ移したこと等により、静的RAMは120 B増えた。常駐ヒープ252 Bとその管理領域を廃止するため、容量削減よりも個別確保と寿命の交差を減らすことが主な効果となる。検索中の88 BのReader本体もヒープへ確保しない。

コンパイル結果の主なスタックフレームは、TM00ページ処理960→1,040 B、Wordbook検索352→448 B、PlaceLookup::searchInto 96→80 B。これらは個別関数の値であり、呼出し全体の最大使用量ではない。32件のページ計画は従来どおりフェーズごとに終了し、TTとTMの計画を同時保持しない。変更後の実機スタック余裕・処理時間・空きヒープは未計測。

## ホスト検証

N03-20260101の同じ７回の検索を、変更前後で実行した。いずれもUI借用バッファを使う経路のC++ newを集計した値で、ピーク常駐量やlibc内部mallocを示すものではない。

| 項目 | 変更前 | 変更後 |
|---|---:|---:|
| C++確保回数 | 82 | 55 |
| C++累積要求バイト | 7,688 B | 3,368 B |

Reader本体27回分の確保を削除した。ホストのReaderサイズはESP32と異なる。残る確保はWordbookの数値配列・文字列等であり、地名検索全体のヒープ廃止ではない。

以下を確認した。

- N03・estatの実データで、通常／UI借用検索の区域コード・地名・キャッシュ結果が一致。
- 最大深さ32、TT32ページ、TM32／33／65ページで逐次SDK Readerと結果が一致。索引と本体のopen／close回数も一致。
- stdio／FatFs、診断有効／無効でReaderと固定FILの借用返却、空範囲、不正範囲、コールバック例外・I/O失敗を確認。
- 読出し・パス変換領域も借用するReader単体の正常処理ではC++ヒープ確保０回。
- 索引生成・CRC・元データ照合・更新失敗時の復旧、地図生成と等倍／縮小表示、GLCの従来逐次読出しと範囲読出しの互換性を確認。

実行した既存ランナーはrun_borrowed_reader.py、run_wordbook_phases.py、run_borrowed_place_files.py、run_place_files.py、run_map_files.py、run_galuchat_ranges.py、run_dataset_index.py、run_quarter_scale.py。実機への書込みはこの変更では実施していない。
