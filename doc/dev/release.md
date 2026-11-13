# ローカルリリースの作成

リポジトリのルートで次を実行する。

```sh
python3 tools/build_release.py
```

スクリプトは`application/Version.h`の`ApplicationVersion`を採用し、PlatformIOの`cardputer-adv`環境でファームウェアをビルドする。続けてGaluchat GIS SDKの`release/`を入力として、既存の`tools/build_mapsets_from_sdk.py`で44セットの実機用マップセットを一時領域へ再生成する。SDKの配置を変えた場合は`--sdk-release`、PlatformIOの実行ファイルを自動検出できない場合は`--pio`を指定する。

成功時は`release/v<version>/`へファームウェアBINを1個、マップセットごとのZIPを44個置く。`release/`はGitの管理対象外とする。同じバージョンの出力先が存在する場合は上書きせず停止する。

```text
geoputer-v<version>-firmware.bin
jp-n03-y2026-1.zip
jp-est-y2020-2.zip
...（マップセットごとに1個）
```

各ZIPは1つのマップセットであり、ZIP直下に`dataset.ini`、`map.glc`、そのマップで利用できる全言語のWordbook、`README.md`、出典・利用条件を示す`NOTICE-map.md`と`NOTICE-en.md`を置く。必要なセットには`NOTICE-ja.md`も置く。ZIP内にディレクトリは作らない。現在は44セットすべてに英語、うち日本の3セットに日本語も収録する。`dataset.ini`の言語セクションと初期言語は変更せず、選択したマップで全Wordbookをすぐに利用できる。端末が生成する`_mapindex.dat`やアラート設定は収録しない。ZIPにチェックサム一覧は収録せず、スクリプト内で全エントリのCRCとINIの参照先を検査する。

端末は`/geoputer/dataset/<識別名>/`を認識する。`<識別名>.zip`を同名のディレクトリへ展開し、そのディレクトリをSDカードの`/geoputer/dataset/`へコピーする。たとえば`jp-n03-y2026-1.zip`なら`/geoputer/dataset/jp-n03-y2026-1/dataset.ini`となる。同梱された言語は設定画面で切り替えられる。

開発中に既存ファームまたは既存`mapsets/`で包装処理だけを試す場合は、`--skip-firmware-build`または`--mapsets mapsets`を指定できる。正式な生成ではどちらも指定せず、ファームとマップセットを新規に生成する。
