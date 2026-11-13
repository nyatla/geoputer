# LoRa内部スリープの実装と検証

2026-10-18。未使用LoRaを起動時のリセット保持から内部cold SLEEPへ移す処理を追加した。GPS測位・記録とUSBストレージの各モードで常に実行し、省電力設定には依存しない。[ハードウェア詳細設計](../03_ハードウェア詳細設計.md)と[タスク構成設計](../06_タスク構成設計.md)に起動手順を反映した。

## 停止手順

setupの先頭は引き続きNRSTをLOWに保持する。Storageは共有SPIを初期化し、SDを非選択にした後にだけNRSTを解除する。起動時に戻るSTDBY_RCをGetStatusで確認し、送受信・PA・周波数の設定を行わずSetSleep(0)を送る。設定保持とRTC起床を使わずLoRa内部のブロックを停止する方式であり、GPSやモジュール全体の給電は切らない。

[Semtech SX1261/2仕様](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1177/DS_SX1261_2_V2-2.pdf)の13.1.1節に従い、SetSleep後はSPIアクセスを止めて1ms待ち、BUSY HIGHを確認する。NSSの下降による起床とリセット変化を防ぐため、NSSとNRSTをHIGHに保持する。起動を繰り返す場合は、GPIOの出力ラッチをLOWに準備してから前回のNRST保持を解除し、安全にリセットへ戻す。

応答不正、BUSYタイムアウト、SLEEPへの移行失敗、制御ピンの保持失敗ではNRST LOWへ戻す。モジュール未接続も最大100msのBUSY待ちで戻り、SDとGPSの起動は継続する。成功時は`[LORA] SLEEP`、失敗時は`[LORA] RESET HELD`を起動ログへ出す。共有SPIはStorageが所有し、新しいタスク・キュー・無線通信ライブラリは追加しない。

## ホスト試験

`python3 tests/run_native.py`は成功した。cold SLEEPの試験では、送るSPIフレームがGetStatusとSetSleep(0)だけであること、TX/RXコマンドを送らないこと、NRESETの1ms以上の保持、SLEEP後の追加SPIアクセス禁止、NSSとNRSTの保持、前回起動の保持を残した再起動を検証した。

モジュール不在、MISOの0/FF、想定外のモード、コマンドエラー、GetStatus後のBUSY固着、SLEEP後のBUSY LOW、NSS保持失敗、32bit時刻の周回を試験し、失敗時のリセット保持と待ち時間の上限を確認した。GPS・SD・I2Cなど別の制御ピンを操作しないことも確認した。既存のキュー・同期、GPS、記録、設定、速度表示、USB、エクスポートの試験を通過した。

## ビルドと実機確認の範囲

PlatformIOのcardputer-advビルドは警告・エラーなしで成功した。静的RAMは77,536バイト、Flashは1,127,797バイト。生成ELFでsetupの最初の関数呼出しがリセット保持であること、SLEEP関数がリンクされていることを確認した。1ms待機に使用するSDKのFreeRTOS tickは1000Hzである。

生成物は`.pio/lora-sleep-firmware.bin`、1,128,160バイト、SHA-256は`b9c0cc40e4b19573a6a1ee914e802da0e48a2025a0c6e6069d7a198e31abad8b`。ログは`.pio/lora-sleep-native.log`、`.pio/lora-sleep-build.log`、`.pio/lora-sleep-setup-assembly.log`、結果は`.pio/lora-sleep-results.json`へ保存した。

このSLEEP版は実機へ未書込みである。端末には[リセット保持・速度表示版](LoRa停止・速度表示検証.md)が書き込まれている。SLEEP時の実電流、RF出力、アンテナなしでの実機動作は測定していない。ソフトウェアによる送信抑止であり、電源投入からGPIO制御開始前、別ファームウェア、外部故障までの安全を保証するものではない。
