# SD_FatFs

SDカードの512バイトセクタ専用FatFsです。ESP-IDF 4.4.7に同梱されたFatFs R0.13cを基に、公開識別子とディスク管理状態を分離しています。SDK標準FatFsと同じプログラム内で併用できます。

ReverseGeoputerではStorageFs / StorageCardを介して通常のSD操作とUSBモードの端末側ファイル操作に採用しています。ArduinoのSDドライバをセクタI/Oとして使用し、専用FatFsと専用VFSで管理します。Config.hのREVERSEGEOPUTER_SD_FATFS=0はSDK版の比較用です。

## 標準版との共存

| 対象 | SDK標準版 | SD専用版 |
| --- | --- | --- |
| 公開ヘッダ | `ff.h` | `SD_FatFs.h` |
| ファイル型 | `FIL` | `SD_FATFS_FIL` |
| ファイルシステム型 | `FATFS` | `SD_FATFS_FATFS` |
| ディレクトリ型 | `FF_DIR` | `SD_FATFS_FF_DIR` |
| 関数 | `f_open` | `sd_fatfs_f_open` |
| フラグ・結果 | `FA_READ` / `FR_OK` | `SD_FATFS_FA_READ` / `SD_FATFS_FR_OK` |
| 最大セクタ長 | SDKの設定（現環境は4096） | 512固定 |
| マウント表・同期状態・disk I/O | SDK内 | ライブラリ内で独立 |

公開名は`SD_FATFS_`または`sd_fatfs_`から始まります。インクルード順序は任意です。SDKの`ff.h`、`ffconf.h`、`sdkconfig.h`やリンク済みFatFsを置き換えません。SD専用の`ff.h`は`src/sd_fatfs/`に置き、通常の`#include <ff.h>`に干渉しない構成にしています。

```cpp
#include <ff.h>
#include <SD_FatFs.h>

FIL sdk_file{};
SD_FATFS_FIL sd_file{};

// それぞれのボリュームを初期化・マウントした後に使用する。
f_open(&sdk_file, "0:/sdk.txt", FA_READ);
sd_fatfs_f_open(&sd_file, "0:/sd.txt", SD_FATFS_FA_READ);
```

両方の`0:`は別の管理空間です。SDK版で内部フラッシュを管理し、SD専用版でSDカードを管理する組合せも可能です。同じ媒体を両方から同時にマウントすると、FATやディレクトリのキャッシュが不整合になるため、媒体の所有者は一方に限定します。所有者を切り替えるときは、全ファイルを閉じてアンマウントします。SDK用のハンドルや`FIL`を専用版に渡すことはできません。

## 設定とメモリ

設定の原本は[tools/ffconf.h.in](tools/ffconf.h.in)です。設定変更後は生成スクリプトを実行します。

- FAT12 / FAT16 / FAT32をサポート。exFATは無効。
- 最小・最大セクタ長を512バイトに固定。初期化時にバックエンドの`GET_SECTOR_SIZE`を確認し、512以外は未初期化として拒否。
- ファイルごとのセクタキャッシュは維持（`FF_FS_TINY=0`）。
- 長いファイル名はスタック作業域を使用（`FF_USE_LFN=2`）、APIはUTF-8。OEM短縮名のコードページはSDKと同じ850。
- 長い名前は最大255 UTF-16コード単位。`FILINFO.fname`はSDK同様256バイトなので、UTF-8の長い表示名が常に全て収まるわけではありません。
- ボリューム数は2、読み書き・フォーマット・属性変更を有効化。fast seekは現SDK設定同様無効。
- 同期はデバイス所有者が用意するロックを使用。待機上限は10,000ミリ秒。ファイル重複オープンの検出はSDK同様無効（`FF_FS_LOCK=0`）。
- FatFs本体とdeviceアダプタはヒープを確保しません。ESP32用の任意VFSアダプタは、マウント時の管理領域とファイル枠、ディレクトリ操作などにmalloc/freeを使用します。`FATFS`、`FIL`、フォーマット用作業域、デバイスのロックは呼出し側が用意します。`f_mkfs`には512バイト以上の作業域を渡します。
- 時刻は`sd_fatfs_set_clock`でFAT形式のローカル時刻を返す関数を指定します。未指定時は1980-01-01 00:00:00です。

ESP32-S3 / Arduino-ESP32 2.0.17でコンパイル時に確認した`FIL`のサイズは次のとおりです。

| 型 | サイズ |
| --- | ---: |
| SDK `FIL` | 4,136バイト |
| `SD_FATFS_FIL` | 552バイト |
| 差 | 3,584バイト減 |

ファイルシステムのセクタ作業域`FATFS.win`も4096から512バイトになります。この削減はアプリと実機ファームへ反映済みです。2種類のFatFsを実際に使用すると両方のコードがFlashに入り、媒体を両方で管理する場合はそれぞれの管理オブジェクトも必要です。

## デバイスの接続

[device.h](src/sd_fatfs/device.h)の`SD_FATFS_Device`に、セクタ単位の初期化・状態取得・読出し・書込み・制御・ロック関数とコンテキストを設定します。

1. `sd_fatfs_bind_device(drive, &device)`で登録する。コールバック表はコピーされますが、コンテキストとロックは登録中有効である必要があります。
2. `SD_FATFS_FATFS`を用意して`sd_fatfs_f_mount(&fs, "0:", 1)`でマウントする。
3. `sd_fatfs_f_*`で操作する。
4. 全ファイルを閉じ、`sd_fatfs_f_mount(nullptr, "0:", 0)`で登録を解除する。
5. `sd_fatfs_unbind_device(drive)`でデバイスを解除する。

`initialize`・`status`はFatFsの状態ビット、`read`・`write`・`ioctl`は`SD_FATFS_DRESULT`を返します。読書きの位置と件数は512バイトセクタ単位です。`ioctl`は少なくとも`CTRL_SYNC`、`GET_SECTOR_SIZE`、`GET_SECTOR_COUNT`、`GET_BLOCK_SIZE`の専用版定数に対応します。セクタ数の戻り値は`SD_FATFS_DWORD`、セクタ長は`SD_FATFS_WORD`、消去単位は`SD_FATFS_DWORD`です。バッファの範囲と媒体の有効範囲はバックエンドで確認します。転送時の媒体取り外し、I/O障害、書込み保護もバックエンドが結果コードで返します。FatFs本体による状態確認と重複しないよう、アダプタは`read`・`write`・`ioctl`の直前に`status`を追加実行しません。

ロックは必須で、`lock(context, timeout_ms)`は取得成功時に非0、タイムアウト時に0を返します。FreeRTOSを使う場合は所有者が保持するMutexと`pdMS_TO_TICKS(timeout_ms)`で実装できます。コールバック自身による追加のヒープ確保はこのライブラリでは管理しません。

登録・解除・マウント・アンマウント・フォーマット・時計設定は、他の操作がない状態で1つの管理タスクから実行します。ファイル操作は登録したロックで保護されますが、同じ`FIL`を複数タスクが操作する場合の順序は呼出し側で管理します。`sd_fatfs_disk_*`を直接呼ぶ場合はロックを経由しないため、呼出し側で排他します。

マウントが失敗しても`FATFS`の登録が残る場合があるため、デバイスを解除する前に必ずアンマウントします。登録済みボリュームがある間の`unbind`は`FR_LOCKED`を返します。パーティション表`SD_FATFS_VolToPart`を変更する場合も、マウント前に実行します。

## 導入と検証

PlatformIOのアプリからは次の依存を指定します。

```ini
lib_deps =
    symlink://third_party/SD_FatFs
```

ホスト検証はGCC、G++、Python 3、ASan / UBSanを使います。

```sh
python3 third_party/SD_FatFs/tests/run_host.py
```

ESP32-S3でSDK標準版と同時にリンクするサンプルを用意しています。このサンプルはSDドライバを接続せず、コンパイル・リンクと型サイズを検証します。

```sh
pio run -d third_party/SD_FatFs/examples/esp32_coexist
```

2026-10-28に次を確認しました。

- C / C++それぞれで両方のヘッダを両順序でインクルードでき、SDK側のセクタ設定が維持される。
- 元のFatFs本体と専用版を同時にリンクし、標準版は4096バイトセクタの仮想ディスク、専用版は512バイトセクタのFAT32仮想ディスクを、同じ論理ドライブ番号`0:`で独立して操作できる。
- UTF-8の日本語ファイル名、連続・非整列の読書き、シーク、同期、ディレクトリ列挙、改名、削除、空き容量取得が動作する。
- デバイス重複登録、マウント中の解除、書込み保護、読出しエラー、範囲外読出し、ロックタイムアウト、512以外のセクタ長を検出する。
- 専用版の公開シンボルが分離され、ESP32-S3で両方のヘッダ順序がビルドでき、ELFにSDK版と専用版の`f_open` / `f_read`が共存する。
- ASan / UBSan付きホストテストが両方のインクルード順序で通る。ホスト64bit ABIの構造体サイズはESP32と異なる。

Cardputer ADVの実SDで、SDK版と専用版を交互に使用する[読出し専用サンプル](examples/cardputer_sd_benchmark/src/main.cpp)も用意しています。N03 / estatのGLC・Wordbook・索引を対象とした360試行で、データと転送量の一致を確認しました。接続層の重複した状態取得を除去した最終版では通常読出しはほぼ同等で、同一セクタ内の1バイト反復は約1.9%増でした。[実機性能比較](../../doc/検証/SD専用FatFsの実機性能比較.md)に条件と数値を記録しています。

```sh
pio run -d third_party/SD_FatFs/examples/cardputer_sd_benchmark
python3 third_party/SD_FatFs/examples/cardputer_sd_benchmark/summarize.py DEVICE.log
```

このサンプルはCardputer ADVの配線と未使用LoRaのリセット保持を含みます。通常ファームを保存してから使用し、計測後に復元します。その後アプリへの接続切替と地図・索引処理の時間／メモリ比較を実施しました。[アプリの実機比較](../../doc/検証/固定FIL2枠とSD専用FatFsの実機比較.md)を参照してください。

## 上流ソースと更新手順

上流は[ESP-IDF v4.4.7のFatFs](https://github.com/espressif/esp-idf/tree/v4.4.7/components/fatfs/src)です。`upstream/esp-idf-v4.4.7/`に原本を無変更で保存し、URLとSHA-256を[upstream/sources.json](upstream/sources.json)に記録しています。[FatFsの設定仕様](https://elm-chan.org/fsw/ff/doc/config.html)も参照できます。

```sh
python3 third_party/SD_FatFs/tools/generate.py
python3 third_party/SD_FatFs/tools/generate.py --check
```

生成処理は公開識別子の接頭辞付与、独立設定への置換、整数型定義のC99固定幅型への統一だけを行います。コメントとライセンス表記を保持し、FATの探索・読書きアルゴリズムは変更しません。`src/sd_fatfs/ff.*`、`diskio.h`、`ffunicode.c`の直接編集は避け、原本・設定・生成規則を更新して再生成します。

FatFs本体とdeviceアダプタは[LICENSE](LICENSE)の条件で利用できます。ESP-IDF由来のVFSは[LICENSE-ESP-IDF](LICENSE-ESP-IDF)のApache License 2.0に従います。FatFsとESP-IDFのライセンスは既にアプリのライセンス一覧へ収録されています。


## ESP32 VFS

ESP_PLATFORM向けにsd_fatfs_vfs_register / sd_fatfs_vfs_unregister_pathを用意しています。ESP-IDF 4.4.7公式vfs_fat.cを基に識別子を分離し、POSIXのDIRを維持して、ファイルシステム型だけを専用版へ接続します。VFSの管理領域・最大ファイル枠は登録時に確保し、ファイルを閉じてFatFsをアンマウントしてからVFSを解除します。SDK用VFSの登録状態は独立しています。

VFSの確保はLFNのメモリフックと分け、malloc/freeを使用します。登録後に返されたSD_FATFS_FATFSを専用版のf_mountへ渡してください。標準版のesp_vfs_fat_registerへ専用版の型を渡すことはできません。アプリでのstdio・stat・日本語ファイル名・ディレクトリ操作と、ローカル／raw／ローカル切替を実機確認済みです。
