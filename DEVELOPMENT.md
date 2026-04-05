# DEVELOPMENT

## 概要

`AviUtl2 MIDI Visualizer 3D` の開発者向けメモです。

## 構成

- [`src/AviUtl2MidiVisualizer3DModule.cpp`](src/AviUtl2MidiVisualizer3DModule.cpp)
  - MIDI を解析して可視ノート区間と拍位置を返す `mod2`
- [`scripts/Piano Roll 3D.obj2`](scripts/Piano Roll 3D.obj2)
  - 3D ピアノロールを描画する `obj2`
- [`tools/build_mod2.ps1`](tools/build_mod2.ps1)
  - `AviUtl2MidiVisualizer3D.mod2` をビルドするスクリプト
- [`tools/build_package.ps1`](tools/build_package.ps1)
  - 配布用パッケージを作成するスクリプト
- [`vendor/aviutl2_sdk`](vendor/aviutl2_sdk)
  - ビルドに必要な最小限の AviUtl2 SDK ヘッダ

## ビルド

```powershell
.\tools\build_mod2.ps1
```

出力先:

- `dist\AviUtl2MidiVisualizer3D.mod2`

バージョン文字列を指定する場合:

```powershell
.\tools\build_mod2.ps1 -Version v1.2.3
```

`Version` はそのまま `mod2` の情報文字列に使われます。既定値は `dev` です。

## パッケージ作成

```powershell
.\tools\build_package.ps1 -Version v1.2.3
```

出力先:

- `dist\aviutl2-midi-visualizer-3d.au2pkg.zip`
- `dist\aviutl2-midi-visualizer-3d.zip`

内側の `.au2pkg.zip` 構成:

- `package.ini`
- `Script\AviUtl2MidiVisualizer3D\AviUtl2MidiVisualizer3D.mod2`
- `Script\AviUtl2MidiVisualizer3D\Piano Roll 3D.obj2`

外側の zip 構成:

- `aviutl2-midi-visualizer-3d.au2pkg.zip`
- `README.md`
- `LICENSE`
- `THIRD_PARTY_NOTICES.md`

## GitHub Actions

- タグ push 時のリリースビルドは [`.github/workflows/release.yml`](.github/workflows/release.yml) で実行します
- `tools/build_mod2.ps1 -Version <tag>` で `mod2` をビルドします
- `build_package.ps1 -Version <tag>` で固定ファイル名の配布パッケージを生成します
- GitHub Release には外側の `aviutl2-midi-visualizer-3d.zip` を添付します
