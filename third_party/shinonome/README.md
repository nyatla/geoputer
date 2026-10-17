# Shinonome 16-dot font data

Source: [code4fukui/shinonome-font](https://github.com/code4fukui/shinonome-font/tree/053b21e0a11ef5799c1ea3cefc374763add80617), revision `053b21e0a11ef5799c1ea3cefc374763add80617`.

The original `LICENSE` declares the font data, documentation, and scripts Public Domain and contains the authors' non-exercise declaration for Japan. The unmodified `LICENSE`, `AUTHORS`, `DESIGN.16`, and original `README` accompany the source data. The repository's modern readme calls the license MIT; this import follows the original `LICENSE` text instead. `source.json` records the exact inputs and SHA-256 checksums.

`16/kanjic/font_src.bit` supplies native 16x16 JIS X 0208 glyphs. `16/latin1/font_src.bit` and `16/hankaku/font_src_diff.bit` supply native 8x16 Latin and half-width kana glyphs. All inputs are retained without modification.

Run `python3 tools/prepare_shinonome.py` from the project root to regenerate `application/task/ui/ShinonomeData.inc`. The generator verifies the input checksums, maps the original JIS font character codes to Unicode, and retains Unicode compatibility forms of punctuation. This build-time font preparation is independent of the firmware's UTF-8-only text input. Native full-width Latin forms are selected for ASCII letters and numbers in the fixed 16x16 cell. Native 8x16 symbols without a full-width counterpart are centered in that cell. Unsupported characters use the native white-square glyph. Glyphs are never doubled from 8x8 or antialiased.

The generated glyph table resides in firmware flash and is read directly. It does not consume the map cache or require the SD card. Small 8x8 UI text uses the existing eight-dot fonts; Shinonome's standard family has 12, 14, 16, and 18-dot sizes.
