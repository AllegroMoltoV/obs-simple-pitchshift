# Test Pitch Shift (OBS Audio Filter Plugin)

OBS Studio 向けの **検証用ピッチシフト音声フィルタ**です。  
標準プロパティ UI（Qt なし）で **半音スライダー（-12..+12）** を出し、`update`（UI スレッド）→ `filter_audio`（音声スレッド）の受け渡しや、`filter_audio` の **返り値バッファ寿命**など、OBS 音声フィルタ実装の土台を確認する目的で作っています。

> 音質は優先しません。低遅延を優先し、クリックやアーティファクトは許容します。

---

## Features / できること

- 音声フィルタ「**検証用ピッチシフト / Test Pitch Shift**」を追加できる
- プロパティで **半音（Semitone）** を `-12..+12` の範囲で変更できる
- `update` と音声処理スレッドの受け渡しを **atomic** で行う例になっている
- `filter_audio` が返す `obs_audio_data*` の **バッファ寿命**を満たす例になっている

---

## Requirements / 動作環境

- Windows 11（x64）
- OBS Studio 32.0.4（32 系）
- サンプルレート: **44.1 kHz 固定**
- チャンネル: **2ch 前提**

---

## Install / インストール（手動）

1. GitHub Releases から zip をダウンロードして展開します。
2. 展開した **プラグインフォルダーごと**、次の場所へコピーします。

- `%PROGRAMDATA%\obs-studio\plugins`

展開後のパス例:

- `%PROGRAMDATA%\obs-studio\plugins\test-pitchshift\bin\64bit\test-pitchshift.dll`
- `%PROGRAMDATA%\obs-studio\plugins\test-pitchshift\data\locale\...`

3. OBS Studio を再起動します。

> すでに同名フォルダーが存在する場合（開発中の `cmake --install` 等で生成済み）は、一度削除してから展開してください。

---

## Usage / 使い方

1. 任意の音声付きソースを選択
2. 「フィルタ」→「音声フィルタ」で **検証用ピッチシフト / Test Pitch Shift** を追加
3. プロパティの **半音（Semitone）** スライダーを操作

---

## Notes / 注意点

- OBS の音声設定が **44.1 kHz 以外**の場合は **バイパス**します。
- **2ch 以外**の構成は想定外です（バイパスします）。
- 検証目的のため、本番運用（重要な配信・収録）では十分にテストしてから使用してください。

---

## Build / ビルド（開発者向け）

このリポジトリは `obs-plugintemplate` ベースです。  
Windows + Visual Studio 2022 を前提とした開発フローになっています。

### 1) Configure（CMake generate）

```bash
cmake --preset windows-x64-local
````

> preset 名は、あなたの環境で使っているものに合わせてください。

### 2) Build

生成された `build_x64/*.sln` を Visual Studio で開き、`Release` / `RelWithDebInfo` などでビルドします。

### 3) Install（ローカル配置）

`cmake --install` で OBS のプラグインフォルダー構造に従って出力できます。

```bash
cmake --install build_x64 --config Release
```

`--prefix dist` を付けると配布用にまとめられます。

```bash
cmake --install build_x64 --config Release --prefix dist
```

---

## CMake formatting (CI) / gersemi について

CI で CMake のフォーマットチェックに `gersemi` を使用しています。
ローカルで整形してから push すると CI で詰まりにくくなります。

インストール例（Python 環境がある場合）:

```bash
pip install --user gersemi
```

整形（in-place）:

```bash
gersemi -i CMakeLists.txt
```

---

## Encoding / 文字コード（Windows）

Windows 環境で日本語コメント等を安全に扱うため、MSVC では `/utf-8` を付ける想定です。
また、`.editorconfig` で `charset = utf-8-bom` とし、ファイル保存時の事故を避けます。

---

## License / ライセンス

OBS Studio の `libobs` を利用するプラグインは、`obs-plugintemplate` の配布ガイドで **GPL の combined work 扱い**になる可能性が説明されています。
バイナリ配布を行う場合、受け取り手が対応する **ソースコードへ到達できる状態**を用意してください。

---

## Links

* OBS Plugin Template: [https://github.com/obsproject/obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
* OBS Docs（Properties / Sources API）: [https://docs.obsproject.com/](https://docs.obsproject.com/)