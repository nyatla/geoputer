# Geoputer mapsets

[日本語](mapsets.md)

Version: v0.1.0

A mapset provides area names and boundaries for Geoputer's coordinate lookup. It is not a road map or satellite image. GPS logging works without a mapset. Place names, boundary maps, area change alerts, and map display in the log viewer require one.

## Choosing a mapset

Check the region, the type and level of administrative or statistical areas, the source year, and the available Wordbook languages. The language used for place names is selected separately from the language used for the device menus.

For Japan, `jp-n03-y2026-1` covers prefectures and municipalities. `jp-est-y2020-2` covers smaller census areas, while `jp-esc-y2026-2` combines coastal small areas with administrative boundaries. The latter two provide more detail, but neither is a building address lookup. All three include Japanese and English Wordbooks. The other mapsets currently include English Wordbooks.

Download the ZIP named after the mapset ID from [GitHub Release v0.1.0](https://github.com/nyatla/geoputer/releases/tag/v0.1.0).

Compare all available sets in the [mapset specification list](mapsets-spec.md).


## ZIP contents and installation

Each ZIP contains `dataset.ini`, `map.glc`, one or more `*.giswordbook` files, and source and license notes in `README.md` and `NOTICE-*.md`. Extract its files directly into `/geoputer/dataset/<mapset ID>/` on a FAT32 microSD card. Multiple mapsets can be stored on the card, but the device uses one at a time. Applying a mapset builds its lookup index. See the [setup manual](setup.md#4-マップセットを選ぶ) for the full steps (Japanese).

The displayed area depends on the source boundaries, grid resolution, and GPS accuracy. Geoputer does not infer a street address from the nearest road or building. Check each ZIP's `README.md` and `NOTICE-*.md` for its source and usage terms.
