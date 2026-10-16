# Galuchat C++ Reader

Galuchat GIS SDKの`cpp/library.properties`が示す0.5.2のReaderを使用する。
2026-10-16時点のローカルSDKから下記3ヘッダを無改変で取り込み、
GeoPuter側の`application/task/storage/galuchat`でファイル検出と表示用変換を行う。
SDKのファイル入力サンプルと同じ`FileReaderFactory`を使用する。

取得元: https://github.com/nyatla/galuchat-gis-sdk/tree/main/cpp/src/galuchat
ライセンス: Apache-2.0（[LICENSE](LICENSE)）。GISデータは同梱しない。

| ヘッダ | SHA-256 |
|---|---|
| `gis_wordbook_reader.hpp` | `d1285e75d95dba365f447adad53499b2803187832a0819ed8c06956424c14189` |
| `std_file_reader.hpp` | `8e4648e969ba3bbe1cd03d30dbc89ecb334efa5d45b060624e67190c15daab22` |
| `wgsmap3_reader.hpp` | `68afb8ab81717b79c511a0bf148931b84fb1b7cdf58d14431979bcf64260979f` |

ローカルSDKのWGSMapSet Readerにはチャンク索引と呼出し元指定の最大チャンク数検証を追加した。
GeoPuter側では`application/Config.h`の`MaxDatasetChunks`（1500）を指定する。SDKのバージョン表記は0.5.2を維持し、上表のハッシュで取り込みソースを識別する。
