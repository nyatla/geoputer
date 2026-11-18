# マップセット仕様一覧

Geoputer v0.1.0向けの実機用マップセットです。[選び方と配置方法](mapsets.md)も参照してください。各識別名のZIPは[GitHub Release v0.1.0](https://github.com/nyatla/geoputer/releases/tag/v0.1.0)にあります。

「格子」はGLCの緯度・経度方向の1セル幅です。「GLC容量」は地図ファイル単体のサイズで、配布ZIPの容量とは異なります。表はリポジトリ管理の[構築元一覧](../tools/mapsets_sources.tsv)と[地図選定記録](dev/mapsets-selection.tsv)から生成しています。マップセット本体のファイルはGit管理に含めません。

<!-- mapset-spec:start -->
## 収録セット（44件）

| 識別名 | 名称 | 区域 | Wordbook | 格子 | GLC容量 |
|---|---|---|---|---:|---:|
| `jp-esc-y2026-2` | 日本・e-Stat海岸線反映区域 | 海岸線を反映した小地区・下位地区と行政区域の統合データ。 | ja, en | 1/10000° | 24.1 MiB |
| `jp-est-y2020-2` | 日本・e-Stat町丁・字等境界 2020 | 国勢調査の町丁・字等と下位地区の区域データ。 | ja, en | 1/10000° | 22.7 MiB |
| `jp-n03-y2026-1` | 日本・行政区域 N03 2026 | 都道府県・市区町村の行政区域データ。 | ja, en | 1/10000° | 4.5 MiB |
| `at-gem-y2026-1` | Austria Municipalities 2026 | Municipalities and Vienna districts. | en | 1/10000° | 1.5 MiB |
| `au-sal-y2021-1` | Australia SAL 2021 | Suburbs and localities. | en | 1/10000° | 13.5 MiB |
| `be-sec-y2026-1` | Belgium Statistical Sectors 2026 | Statistical sector boundaries. | en | 1/10000° | 3.1 MiB |
| `br-dis-y2022-1` | Brazil Districts 2022 | District boundaries from Census 2022. | en | 1/10000° | 21.3 MiB |
| `ca-csd-y2021-1` | Canada CSD 2021 | Census subdivisions. | en | 1/10000° | 24.3 MiB |
| `ch-sb3-y2026-1` | Switzerland Boundaries 2026 | swissBOUNDARIES3D base areas. | en | 1/10000° | 1.0 MiB |
| `ci-spf-y2021-1` | Ivory Coast Subprefectures 2021 | Subprefecture boundaries. | en | 1/10000° | 0.9 MiB |
| `cl-dis-y2023-1` | Chile Census Districts 2023 | Census districts from APC2023. | en | 1/10000° | 7.2 MiB |
| `cy-adm-y2024-1` | Cyprus Administrative Areas 2024 | Administrative units and quarters. | en | 1/10000° | 0.4 MiB |
| `cz-rui-y2026-1` | Czechia RUIAN 2026 | Municipalities and military areas. | en | 1/10000° | 2.9 MiB |
| `de-vg-y2026-1` | Germany VG250 2026 | Municipalities and unincorporated areas. | en | 1/10000° | 6.9 MiB |
| `dk-dag-y2026-1` | Denmark DAGI 2026 | Municipalities and areas outside municipal division. | en | 1/10000° | 0.5 MiB |
| `ee-asu-y2026-1` | Estonia Settlements 2026 | Settlement boundaries. | en | 1/10000° | 1.8 MiB |
| `es-lim-y2026-1` | Spain Administrative Areas 2026 | Municipalities and special areas. | en | 1/10000° | 5.9 MiB |
| `fi-kun-y2026-1` | Finland Municipalities 2026 | Municipality boundaries. | en | 1/10000° | 1.3 MiB |
| `fr-aec-y2026-1` | France Admin Express 2026 | Communes and municipal arrondissements. | en | 1/10000° | 15.6 MiB |
| `gb-lad-y2025-1` | UK LAD 2025 | Local Authority Districts, full clipped boundaries. | en | 1/10000° | 2.2 MiB |
| `hr-au-y2026-1` | Croatia Settlements 2026 | Settlement boundaries. | en | 1/10000° | 2.3 MiB |
| `is-au-y2022-1` | Iceland Municipalities 2022 | Municipality boundaries. | en | 1/10000° | 0.6 MiB |
| `it-lim-y2026-1` | Italy Municipalities 2026 | Municipality boundaries. | en | 1/10000° | 5.2 MiB |
| `kr-sgi-y2025-1` | Korea SGIS 2025 | Administrative dong boundaries. | en | 1/10000° | 2.2 MiB |
| `lr-cln-y2022-1` | Liberia Clans 2022 | Clan administrative boundaries. | en | 1/10000° | 0.6 MiB |
| `lt-au-y2026-1` | Lithuania Administrative Areas 2026 | Districts and undivided municipality areas. | en | 1/10000° | 0.8 MiB |
| `lu-cad-y2026-1` | Luxembourg Cadastral Areas 2026 | Cadastral sections and shared areas. | en | 1/10000° | 0.2 MiB |
| `lv-var-y2026-1` | Latvia VARIS 2026 | Cities and pagasts from the address register. | en | 1/10000° | 0.8 MiB |
| `me-ops-und-1` | Montenegro Municipalities undated | Source municipality divisions; boundary year unknown. | en | 1/10000° | 82 KiB |
| `nl-adm-y2026-1` | Netherlands Municipalities 2026 | Municipality boundaries. | en | 1/10000° | 0.6 MiB |
| `nl-buu-y2026-1` | Netherlands Wijk/Buurt 2026 | District and neighbourhood boundaries. | en | 1/10000° | 2.5 MiB |
| `nz-sa2-y2026-1` | New Zealand SA2 2026 | Statistical Area 2 boundaries. | en | 1/10000° | 2.2 MiB |
| `qa-mun-y2015-1` | Qatar Municipalities 2015 | Municipality boundaries from the 2015 census source. | en | 1/10000° | 57 KiB |
| `ro-uat-y2025-1` | Romania UAT 2025 | Local administrative areas and Bucharest sectors. | en | 1/10000° | 3.1 MiB |
| `rs-set-und-1` | Serbia Settlements undated | Settlement boundaries; boundary year unknown. | en | 1/10000° | 2.1 MiB |
| `se-reg-y2025-1` | Sweden RegSO 2025 | Regional statistical areas. | en | 1/10000° | 2.7 MiB |
| `sg-sub-y2019-1` | Singapore Subzones 2019 | Master Plan 2019 subzones, excluding sea. | en | 1/10000° | 50 KiB |
| `si-rpe-y2026-1` | Slovenia RPE 2026 | Settlement boundaries. | en | 1/10000° | 1.3 MiB |
| `sk-zbg-y2026-1` | Slovakia ZBGIS 2026 | Municipal-level administrative areas. | en | 1/10000° | 1.3 MiB |
| `tw-vil-y2026-1` | Taiwan Villages 2026 | County, township and village boundaries. | en | 1/10000° | 1.3 MiB |
| `us-cou-y2025-1` | US Counties 2025 | Counties and county equivalents. | en | 1/10000° | 7.1 MiB |
| `us-sta-y2025-1` | US States 2025 | States and state equivalents. | en | 1/10000° | 2.0 MiB |
| `us-sub-y2025-1` | US COUSUB 2025 | County subdivisions and equivalents. | en | 1/10000° | 23.2 MiB |
| `ww-adm-y2024-2` | World CGAZ 2024 | Best available administrative regions. | en | 1/1000° | 20.4 MiB |
<!-- mapset-spec:end -->
