# AviUtl2 MIDI Visualizer 3D

`AviUtl2 MIDI Visualizer 3D` は、AviUtl2 上で MIDI を 3D ピアノロールとして表示するプラグインです。  
表現設計の一部では MIDITrail `PianoRoll3D` を参考にしていますが、本プラグインは AviUtl2 向けに独立して実装されています。

- `X`: 時間
- `Y`: 音高
- `Z`: MIDI チャンネル

## できること

- MIDI ノートを 3D 空間上のノートボックスとして表示
- 再生位置の断面スラブ表示
- 発音直後のノート強調表示
- 発音位置のリップル表示
- グリッドと拍フレームの表示
- `CHANNEL / SCALE` の 2 種類の色モード
- AviUtl2 のカメラ制御と組み合わせた表示

## 反映する MIDI 情報

- `Note On / Note Off`
  - ノートの開始位置と長さに反映します
- `Velocity`
  - 発音直後の強調表現に反映します
- `Pitch Bend`
  - ノートの高さ移動に反映します
- `Tempo`
  - 時間軸上の再生位置計算に反映します
- `Time Signature`
  - 拍フレームと小節頭フレームに反映します

## 導入方法

GitHub Release の外側 zip から `aviutl2-midi-visualizer-3d.au2pkg.zip` を取り出し、AviUtl2 のプレビュー画面へ D&D してください。

手動で配置する場合は、次の 2 ファイルを同じフォルダへ置いてください。

- `AviUtl2MidiVisualizer3D.mod2`
- `Piano Roll 3D.obj2`

例:

```text
ProgramData\aviutl2\Script\AviUtl2MidiVisualizer3D\AviUtl2MidiVisualizer3D.mod2
ProgramData\aviutl2\Script\AviUtl2MidiVisualizer3D\Piano Roll 3D.obj2
```

## 主な調整項目

- `時間軸スケール`
- `表示前方秒数`
- `表示後方秒数`
- `音高間隔`
- `チャンネル間隔`
- `ノート高さ`
- `ノート幅`
- `グリッド線幅`
- `拍フレーム透明度`
- `強拍フレーム透明度`
- `再生断面透明度`
- `再生断面厚み`
- `発音強調時間ms`
- `発音時サイズ倍率x100`
- `発音時白寄せx100`
- `リップル時間ms`
- `リップル倍率x100`
- `色モード`
- `Ch-01` から `Ch-16`
- `Scale-01` から `Scale-12`

## 開発者向け情報

- ビルド、パッケージ、Actions については [`DEVELOPMENT.md`](DEVELOPMENT.md) を参照してください

## ライセンス

- 本プロジェクト本体: [`LICENSE`](LICENSE)
- サードパーティ表記: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)

MIDITrail の扱いについては [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) を参照してください。

## 免責

本プラグインの利用、導入、設定、配布物の適用、およびそれらに起因するいかなる結果についても、利用者自身の責任で行ってください。  
作者は、本プラグインの利用によって生じた直接的または間接的な損害について責任を負いません。
