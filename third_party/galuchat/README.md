# Galuchat C++ Reader

Galuchat GIS SDKの`cpp/library.properties`が示す0.5.2のReaderを使用する。
2026-10-21時点のローカルSDKから下記4ヘッダを無改変で取り込み、
ReverseGeoputer側の`application/task/storage/galuchat`でファイル検出と表示用変換を行う。
ファイル入力はSDK互換のアプリ側`ObservedFileReaderFactory`を使用し、SDアクセスと所要時間を観測する。

取得元: https://github.com/nyatla/galuchat-gis-sdk/tree/main/cpp/src/galuchat
ライセンス: Apache-2.0（[LICENSE](LICENSE)）。GISデータは同梱しない。

| ヘッダ | SHA-256 |
|---|---|
| `gis_wordbook_reader.hpp` | `d1285e75d95dba365f447adad53499b2803187832a0819ed8c06956424c14189` |
| `std_file_reader.hpp` | `8e4648e969ba3bbe1cd03d30dbc89ecb334efa5d45b060624e67190c15daab22` |
| `wgsmap3_reader.hpp` | `8a1a03d2053510ad38caa8e0d1acdd72d27bc108b0054e13a3e8667294bbe651` |
| `wgsmapset3_indexed_reader.hpp` | `5f5ce2f90558d970612db53f1e5e64eb75d886fd4ff2a9dcbf3bf7e36dee19cd` |

従来の`GaluchatWGSMapSet3Reader`は索引追加前の順次走査を維持する。索引方式は別ヘッダの`GaluchatWGSMapSet3IndexedReader`として明示的に選択する。アプリの`CachedMapReader`はこちらを使用し、`application/Config.h`の`MaxDatasetChunks`（1500）を指定する。キャッシュ再構築は`readWgsRectWithoutTail()`で最後の必要ブロックまで取得し、後続のブロック走査と末尾への移動を省く。必要ブロック内部の復号とRAWS/LZSSの終端検証は維持し、GI01全体長の一致と未使用の末尾は検証しない。SDKのバージョン表記は0.5.2を維持し、上表のハッシュで取り込みソースを識別する。
