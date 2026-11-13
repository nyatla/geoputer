# ローカルリリースの作成

リポジトリのルートで次を実行する。

```sh
python3 tools/build_release.py
```

スクリプトは`application/Version.h`の`ApplicationVersion`を採用し、PlatformIOの`cardputer-adv`環境でファームウェアをビルドする。続けてGaluchat GIS SDKの`release/`を入力として、既存の`tools/build_mapsets_from_sdk.py`で44セットの実機用マップセットを一時領域へ再生成する。SDKの配置を変えた場合は`--sdk-release`、PlatformIOの実行ファイルを自動検出できない場合は`--pio`を指定する。

成功時は`release/v<version>/`へファームウェアBINを1個、マップセットごとのZIPを44個置く。`release/`はGitの管理対象外とする。同じバージョンの出力先が存在する場合は上書きせず停止する。

```text
geoputer-v<version>-firmware.bin
geoputer-v<version>-mapset-jp-n03-y2026-1.zip
geoputer-v<version>-mapset-jp-est-y2020-2.zip
...（マップセットごとに1個）
```

各ZIPは1つのマップセットであり、`mapsets/<識別名>/`に元の`dataset.ini`、`map.glc`、そのマップで利用できる全言語のWordbookを置く。現在は44セットすべてに英語、うち日本の3セットに日本語も収録する。`dataset.ini`の言語セクションと初期言語は変更せず、選択したマップで全Wordbookをすぐに利用できる。端末が生成する`_mapindex.dat`やアラート設定は収録しない。ZIP直下の`README.md`にSDへの配置方法、`notices/`に地図とWordbookの構築元の表示を収録する。ZIPにチェックサム一覧は収録せず、スクリプト内で全エントリのCRCとINIの参照先を検査する。

端末は識別名ではなく`/dataset/01/`〜`/dataset/99/`を認識する。ZIP内のセットを選び、その**中身**を未使用の番号ディレクトリへコピーする。同梱された言語は設定画面で切り替えられる。

開発中に既存ファームまたは既存`mapsets/`で包装処理だけを試す場合は、`--skip-firmware-build`または`--mapsets mapsets`を指定できる。正式な生成ではどちらも指定せず、ファームとマップセットを新規に生成する。
