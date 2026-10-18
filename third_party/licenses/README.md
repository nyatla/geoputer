# ライセンス情報

現在のファームウェアに使用するライブラリとフォントの表示用本文を保持する。名称・採用版・本文ファイル・取得元は[manifest.json](manifest.json)に記載する。ローカルパスはPlatformIOで採用した配布物内の取得元を示す。

ライブラリ本体のライセンスに加え、M5Cardputer内のAdafruitドライバー、M5GFX内のLovyanGFX・TJpgDec、採用フォント、Arduino-ESP32・ESP-IDF・FreeRTOS・FatFs・TinyUSB・Galuchat C++ Readerの本文を収録する。フォントの配布条件はライブラリ本体と別に扱う。配布物を更新するときは取得元・版・著作権表示・本文を確認して一覧を更新する。

`python3 tools/prepare_licenses.py`で`application/task/ui/LicenseText.cpp`を生成する。元の本文は本ディレクトリに保存し、画面表示はASCII 8×8ドット・固定幅29文字の行へ折り返す。生成後はビルドとホストテストを実行する。
