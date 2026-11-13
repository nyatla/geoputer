# Galuchat C++ Reader

Galuchat GIS SDKの`cpp/library.properties`が示す0.5.2のReaderを使用する。
2026-10-21時点のローカルSDKから下記4ヘッダを取り込んだものを基準とし、
ReverseGeoputer側の`application/task/storage/galuchat`でファイル検出と表示用変換を行う。
ファイル入力はSDK互換のアプリ側`ObservedFileReaderFactory`を使用し、SDアクセスと所要時間を観測する。

取得元: https://github.com/nyatla/galuchat-gis-sdk/tree/main/cpp/src/galuchat
ライセンス: Apache-2.0（[LICENSE](LICENSE)）。GISデータは同梱しない。

取込時の上流ヘッダのSHA-256（下記のアプリ限定試作前）:

| ヘッダ | SHA-256 |
|---|---|
| `gis_wordbook_reader.hpp` | `d1285e75d95dba365f447adad53499b2803187832a0819ed8c06956424c14189` |
| `std_file_reader.hpp` | `8e4648e969ba3bbe1cd03d30dbc89ecb334efa5d45b060624e67190c15daab22` |
| `wgsmap3_reader.hpp` | `8a1a03d2053510ad38caa8e0d1acdd72d27bc108b0054e13a3e8667294bbe651` |
| `wgsmapset3_indexed_reader.hpp` | `5f5ce2f90558d970612db53f1e5e64eb75d886fd4ff2a9dcbf3bf7e36dee19cd` |

従来の`GaluchatWGSMapSet3Reader`は索引追加前の順次走査を維持する。SDKの索引方式は別ヘッダの`GaluchatWGSMapSet3IndexedReader`として明示的に選択でき、比較試験の旧方式で使用する。現在の製品はアプリの`DiskIndexedMapReader`で保存索引を読み、チャンク・ブロック群の配列をRAMへ常駐させない。SDKのバージョン表記は0.5.2を維持し、上表のハッシュで取り込み基準を識別する。

## アプリ限定の範囲Reader試作

2026-10-22時点では、次の3ヘッダにReverseGeoputer内だけの差分を保持する。
Galuchat coreとSDKには未反映であり、SDKから再コピーすると試作が失われる。

- `ReaderFactory::createRange(offset, length)`を追加した。既存Factoryには所有権付きの論理範囲Readerで後方互換の既定実装を提供する。`create(offset)`は維持する。`BufferReaderFactory`はメモリ範囲を検証して直接生成する。
- 索引の各要素は、絶対開始位置の代わりにGI01全体のバイトサイズを保持する。識別子とMBUInt長フィールドを含む。最初のGI01の位置とサイズ列の累積和で各開始位置を求め、索引要素数・容量は増やさない。
- `readWgsRectWithoutTailBounded()`を、出力先指定・RawRaster返却の2形式で追加した。各GI01の開始位置とサイズをFactoryへ渡す。従来の矩形APIは範囲指定を使わない。
- アプリの`ObservedFileReaderFactory`は範囲指定を実装し、バッファを`min(用途別上限, 範囲長)`で確保する。補充は範囲末尾までに制限する。既定の論理範囲Readerだけでは、下層ファイルReaderの物理的な先読み量までは制御しない。
- `GaluchatImageDataChunk01Reader::visitBlockPositions`は、索引生成用に全ブロックの位置・長さを列挙する単回利用APIである。
- `GaluchatImageDataChunk01Reader::readRectFromBlockRange(source, range, x, y, dest)`は、外部索引で位置決めしたByteReaderから矩形の必要部分を復号する静的APIである。`BlockRange{width,height,square_unit,first_block,block_count,data_bytes}`を渡す。sourceは群先頭のBlockHeader・バイト境界へ置く。GI01ヘッダーを読まず、画像全体の座標で描画し、最後の必要ブロックで停止する。長さは群のバイト範囲に制限し、全群を消費した場合は末尾位置も照合する。RAWの構造・ビット整列・RAWS/LZSSの終端検証は既存の復号処理で行う。
- 既存のGI01コンストラクタ・矩形メソッドと逐次Readerは変更せず、追加APIを明示的に呼んだ場合だけ群の途中再開を使用する。逐次Readerのトレースは従来の黄金値と照合する。

## アプリ限定のRLE・単色領域の区間描画

出力先が`setSpan(x, y, count, step_x, step_y, value)`を持つ場合、RLEでは
ランを行または列の境界で分割し、矩形との交差を区間単位で計算する。
`count`は正数、`(step_x, step_y)`は`(1,0)`・`(-1,0)`・`(0,1)`・`(0,-1)`のいずれか、
`value`は`int64_t`である。始点と区間内の全座標は出力先の有効範囲内となる。
出力先は指定された順序で画素を書き込む。4種類の走査モードを維持し、追加バッファを確保しない。

`set`のみを持つ出力先は従来の画素単位処理を使用する。既存の矩形API・入力ストリームの消費・
RLEのラン復号と検証・RAW・LZSSは変更しない。
アプリの`DiskIndexedMapReader`だけがこの追加能力を利用し、ゼロを透過し、
先に得た非ゼロの区域コードを保持する。Core・SDKへの同期は別途行う。
単色領域の`fillRect`も、区間書込み対応の出力先には1行ずつ書き込む。RLEと単色領域の能力判定は`HasSpan`へ統一する。RLEはランを行・列の境界で分割し、それぞれ矩形との交差を判定する従来の区間描画を維持する。分割前に走査範囲を絞る試作は、測定で明確な効果を確認できず、実装の簡潔さを優先して取り下げた。`set`のみの出力先は両方とも従来経路を使う。このRLE・単色領域の変更では追加バッファ・索引形式・ビット読出し・RAWは対象としない。追加分の検証は[単色領域の区間描画とRLE範囲除外](../../doc/dev/検証/単色領域の区間描画とRLE範囲除外.md)を参照する。

最初の区間描画導入時のホスト検証と実機比較は[RLE区間描画の最適化](../../doc/dev/検証/RLE区間描画の最適化.md)を参照する。

## アプリ限定のRAW行処理

RAWセルは矩形との交差をセルごとに求め、描画対象の行を先に判定する。
画素ごとの除算・剰余と上下の範囲判定をなくし、行内は横方向の範囲だけを確認する。
直接値とパレット値で同じ行処理を使い、復号箇所を一つに保つ。描画部分は出力x座標を加算する。
直接MBUInt値と1・2・4bitパレットの復号器もセルごとに選択する。
範囲外を含む全画素は同じ順序・同じ読出し操作で復号し、入力位置、ビット位置、
切断・不正値の検出、RAWS/LZSSの終端検証を維持する。`skipRaw`やByteReaderは変更しない。
行バッファ、追加のヒープ領域、出力先の新しいAPIは不要である。
Core・SDKへは未反映。互換性とホスト比較は[RAW行処理の検証](../../doc/dev/検証/RAW行処理の検証.md)を参照する。

## アプリ限定のGI01 Reader保持方式

`GaluchatImageDataChunk01Reader`は、ペイロードの`LimitedReader`を値メンバーとして保持する。
チャンク単位・ブロック群単位の生成で`make_unique`を行わず、呼出し側のデコーダと同じ寿命にする。
従来の入力位置・ビット位置・末尾検証、単回利用の制約、コピー禁止とムーブ可能な性質を維持する。
メンバー自身を指すポインタを置かず、ムーブ後も入力元への参照と読出し状態を維持する。
アプリ内の変更であり、Core・SDKへの同期は別途行う。
[地図生成の残存ヒープ調査](../../doc/dev/検証/地図生成の残存ヒープ調査.md)にFactoryのスタック化と合わせて記録する。

## アプリ限定のWordbook索引用API

`gis_wordbook_reader.hpp`の既存コンストラクタ・逐次検索メソッドは変更せず、外部索引で位置決めしたByteReaderから読む静的APIを追加する。API自身は索引を保持しない。

- `readCodeSetFromRoot(source, range, target)`：最上位の枝から階層コードを得る。`IndexRootRange`はdepth・配下件数・コードビット幅・開始ビット・枝のビット長・最終枝フラグを持つ。targetは枝内の相対区域index。最後の枝だけ最大7ビットの末尾paddingを許容する。
- `readTokenIdSetsFromPage(source, page_bytes, first_code, record_count, token_bits, targets)`：TT00ページの先頭から必要文字列のトークン列を得る。targetsは全体の文字列コードを昇順・重複なしで渡す。ページ件数・長さを照合し、既存のパケット・パレット復号を使う。
- `readTokensFromPage(source, first_token, count, token_bytes, targets)`：TM00の固定長TokenStream範囲から断片を得る。sourceは最初の断片に置き、targetsは全体のトークン番号を昇順・重複なしで渡す。

枝・ページの本文を途中から任意に復号せず、独立した最上位要素・ページの境界から開始する。Core・SDKへは未反映であり、同期は別途行う。利用・正しさ・I/Oの検証は[保存Wordbook索引の検証](../../doc/dev/検証/保存Wordbook索引の検証.md)を参照する。

試作ヘッダの現在のSHA-256:

| ヘッダ | SHA-256 |
|---|---|
| `gis_wordbook_reader.hpp` | `27018e7f168ac18ad8f9bf9751ca74be61342e57b275e5941e23a28f539af8aa` |
| `wgsmap3_reader.hpp` | `1af63584a21daf4dc02204b4dc0851b1d5d576ecea5679ab2fb6c9c6b2f25601` |
| `wgsmapset3_indexed_reader.hpp` | `bab035394f1b82c13511d8e2bfa7d9b0140c54ddcb9209e041c0e2534848287f` |

ホスト検証はリポジトリのルートで`python3 tests/run_galuchat_ranges.py`を実行する。
既存の矩形・逐次トレース試験はSDKから同一内容を取り込み、新しい範囲試験とともにアプリ内ヘッダを対象に実行する。
実ファイル・アプリ側キャッシュは`tests/run_place_files.py`、freadの範囲制限は`tests/observed_file_reader_test.cpp`で検証する。
性能比較は[チャンク範囲Readerの実機比較](../../doc/dev/検証/チャンク範囲Readerの実機比較.md)に記録する。

保存索引の群選択・直接位置決め・複数群のハンドル共有は[保存ブロック群索引の検証](../../doc/dev/検証/保存ブロック群索引の検証.md)を参照する。Core・SDKへの同期は別途行う。
