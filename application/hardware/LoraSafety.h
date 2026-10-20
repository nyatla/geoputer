#pragma once

class SPIClass;

namespace geoputer {

// setup()の先頭でLoRaをリセット保持する。GPS/USB両モード共通。
// 出力ラッチを先に設定し、前回起動のGPIO保持を安全に置き換える。
// SLEEPへの移行前と失敗時はNRST LOWで送信を禁止する。物理電源断ではない。
void holdUnusedLoraInReset();

// Storage所有のSPIを初期化済み、SDのCSをHIGHとした状態で一度だけ呼ぶ。
// TX/RXを開始せず、GetStatusとSetSleep(0)だけを送ってLoRa内部を停止する。
// 成功時はNRST/NSSをHIGHに保持して復帰を防ぐ。失敗時はリセット保持へ戻す。
// 成功はSTDBY応答とSLEEP後のBUSY HIGHに基づく。電流/RF出力の実測ではない。
// observed_statusは任意の起動診断用。GetStatus未実行時は0xffを返す。
bool sleepUnusedLora(SPIClass& spi, unsigned char* observed_status = nullptr);

} // namespace geoputer
