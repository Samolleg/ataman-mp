# Synthetic heightmap fixtures

These grayscale PNGs are deterministic editor test resources. They contain no Evil Islands assets.
All images are 257 x 257 pixels so their center lines and corner orientation are exact.

Suggested first import range: black `8000`, white `48000`. Import uses **Stretch to terrain**,
so the images work with maps of different sector dimensions.

- `orientation_corners_257.png`: four distinct corner levels plus contrasting top/bottom markers.
  Use it to verify **Flip image vertically**.
- `radial_hill_257.png`: smooth central hill falling to zero at the outer radius.
- `diagonal_ramp_257.png`: continuous southwest-to-northeast slope.
- `terraced_basin_257.png`: concentric stepped levels with a low center.
- `seam_cross_257.png`: two narrow ridges crossing exactly at image row/column 128. Use it to
  inspect sector-seam continuity after stretching.

Expected behavior:

1. The confirmation dialog reports `257 x 257` samples and the chosen orientation/range.
2. Import appears as one undo entry.
3. Undo restores the prior terrain exactly; redo restores the imported preview.
4. Changed-MPR saving remains refused in this development stage.

SHA-256 inventory:

- `orientation_corners_257.png`: `785A878529F40CD2E7C36A225A33C04E49F32C56E48E0348AF76EF60475F7652`
- `radial_hill_257.png`: `42BB904DBC919F01FDBBABA23FF99E092115FB8220FFC78997ABBD42419BBB20`
- `diagonal_ramp_257.png`: `5F583B3B6BCFFC473052BDDE3FFAE582391FA80F9ED1E7FE95D7DEF8F69E4F0C`
- `terraced_basin_257.png`: `1FCF0D95921EDFB0ECAF619D34422243FB683CFFC9635B9F4BC3FE9C89E6F7A4`
- `seam_cross_257.png`: `106610CFC72F71DB665AEB7A1C477E4FE5B295DF3CC479C5D7CAE6AEE152EC59`
