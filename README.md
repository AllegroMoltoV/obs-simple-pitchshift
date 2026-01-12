# Simple Pitch Shift (OBS Audio Filter Plugin)

OBS Studio 向けの **シンプルなピッチシフト音声フィルタ**です。
プロパティ UI の **半音スライダー（-12..+12）** を操作して、音声のピッチを変更できます。

> 低遅延を優先した実装のため、音質よりもリアルタイム性を重視しています。

---

## Features / できること

- 音声フィルタ「**シンプルピッチシフト / Simple Pitch Shift**」を追加できる
- プロパティで **半音（Semitone）** を `-12..+12` の範囲で変更できる
- **44.1 kHz** と **48 kHz** の両方のサンプルレートに対応
- 低遅延（約 8.7ms）でリアルタイム処理

---

## Requirements / 動作環境

- Windows 11（x64）
- OBS Studio 32.x 系
- サンプルレート: **44.1 kHz** または **48 kHz**
- チャンネル: **2ch（ステレオ）**

---

## Install / インストール（手動）

1. GitHub Releases から zip をダウンロードして展開します。
2. 展開した **プラグインフォルダーごと**、次の場所へコピーします。

- `%PROGRAMDATA%\obs-studio\plugins`

展開後のパス例:

- `%PROGRAMDATA%\obs-studio\plugins\simple-pitchshift\bin\64bit\simple-pitchshift.dll`
- `%PROGRAMDATA%\obs-studio\plugins\simple-pitchshift\data\locale\...`

3. OBS Studio を再起動します。

> すでに同名フォルダーが存在する場合は、一度削除してから展開してください。

---

## Usage / 使い方

1. 任意の音声付きソースを選択
2. 「フィルタ」→「音声フィルタ」で **シンプルピッチシフト / Simple Pitch Shift** を追加
3. プロパティの **半音（Semitone）** スライダーを操作

---

## Notes / 注意点

- OBS の音声設定が **44.1 kHz / 48 kHz 以外**の場合は **バイパス**します。
- **2ch 以外**の構成は想定外です（バイパスします）。
- 低遅延優先のため、クリックやアーティファクトが発生する場合があります。

---

## Build / ビルド（開発者向け）

このリポジトリは `obs-plugintemplate` ベースです。
Windows + Visual Studio 2022 を前提とした開発フローになっています。

### 1) Configure（CMake generate）

```bash
cmake --preset windows-x64
```

### 2) Build

生成された `build_x64/*.sln` を Visual Studio で開き、`Release` / `RelWithDebInfo` などでビルドします。

### 3) Install（ローカル配置）

```bash
cmake --install build_x64 --config Release
```

配布用にまとめる場合:

```bash
cmake --install build_x64 --config Release --prefix dist
```

---

## CMake formatting (CI) / gersemi について

CI で CMake のフォーマットチェックに `gersemi` を使用しています。

```bash
pip install --user gersemi
gersemi -i CMakeLists.txt
```

---

## License / ライセンス

OBS Studio の `libobs` を利用するプラグインは、`obs-plugintemplate` の配布ガイドで **GPL の combined work 扱い**になる可能性が説明されています。
バイナリ配布を行う場合、受け取り手が対応する **ソースコードへ到達できる状態**を用意してください。

---

## Links

* OBS Plugin Template: [https://github.com/obsproject/obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
* OBS Docs: [https://docs.obsproject.com/](https://docs.obsproject.com/)
