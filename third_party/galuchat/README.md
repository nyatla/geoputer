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

従来の`GaluchatWGSMapSet3Reader`は索引追加前の順次走査を維持する。索引方式は別ヘッダの`GaluchatWGSMapSet3IndexedReader`として明示的に選択する。アプリの`CachedMapReader`はこちらを使用し、`application/Config.h`の`MaxDatasetChunks`（1500）を指定する。キャッシュ再構築はアプリ限定試作の`readWgsRectWithoutTailBounded()`で最後の必要ブロックまで取得し、後続のブロック走査と末尾への移動を省く。必要ブロック内部の復号とRAWS/LZSSの終端検証は維持し、GI01全体長の一致と未使用の末尾は検証しない。SDKのバージョン表記は0.5.2を維持し、上表のハッシュで取り込み基準を識別する。

## アプリ限定の範囲Reader試作

2026-10-21時点では、次の2ヘッダにReverseGeoputer内だけの差分を保持する。
Galuchat coreとSDKには未反映であり、SDKから再コピーすると試作が失われる。

- `ReaderFactory::createRange(offset, length)`を追加した。既存Factoryには所有権付きの論理範囲Readerで後方互換の既定実装を提供する。`create(offset)`は維持する。`BufferReaderFactory`はメモリ範囲を検証して直接生成する。
- 索引の各要素は、絶対開始位置の代わりにGI01全体のバイトサイズを保持する。識別子とMBUInt長フィールドを含む。最初のGI01の位置とサイズ列の累積和で各開始位置を求め、索引要素数・容量は増やさない。
- `readWgsRectWithoutTailBounded()`を、出力先指定・RawRaster返却の2形式で追加した。各GI01の開始位置とサイズをFactoryへ渡す。従来の矩形APIは範囲指定を使わない。
- アプリの`ObservedFileReaderFactory`は範囲指定を実装し、バッファを`min(用途別上限, 範囲長)`で確保する。補充は範囲末尾までに制限する。既定の論理範囲Readerだけでは、下層ファイルReaderの物理的な先読み量までは制御しない。
- `GaluchatWGSMapSet3Reader`と`GaluchatImageDataChunk01Reader`のクラス本体は試作前と同一。逐次Readerのトレースは従来の黄金値と照合する。

試作ヘッダの現在のSHA-256:

| ヘッダ | SHA-256 |
|---|---|
| `wgsmap3_reader.hpp` | `8c9370dc8d6ca05a39500dc00532bf8b88238b501887d6077d1fc7bc3e09a5bf` |
| `wgsmapset3_indexed_reader.hpp` | `bab035394f1b82c13511d8e2bfa7d9b0140c54ddcb9209e041c0e2534848287f` |

ホスト検証はリポジトリのルートで`python3 tests/run_galuchat_ranges.py`を実行する。
既存の矩形・逐次トレース試験はSDKから同一内容を取り込み、新しい範囲試験とともにアプリ内ヘッダを対象に実行する。
実ファイル・アプリ側キャッシュは`tests/run_place_files.py`、freadの範囲制限は`tests/observed_file_reader_test.cpp`で検証する。
性能比較は[チャンク範囲Readerの実機比較](../../doc/検証/チャンク範囲Readerの実機比較.md)に記録する。
