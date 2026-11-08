# v1.1.0 の事前調査

2026-11-08 に、処理窓の短縮とスライダー追加、v1.1.0 の公開に向けて現状を調査した。計画の正本は Beads の親課題に置く。

## 調査範囲

`main` の `3992dc5` を対象に、`src/voice-pitch-shifter.h`、`src/test-pitchshift-filter.cpp`、`tests/pitch-obs-probe.cpp`、`data/locale`、`buildspec.json`、`CMakeLists.txt`、`.github/workflows/push.yaml` を確認した。依存の Signalsmith Stretch `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da` のヘッダーで公開 API を確認した。GitHub の ruleset と Release は `gh` で取得した。Forum はリソース 2338 の公開ページを取得した。

## 検証結果

### 音声処理

`VoicePitchShifter::prepare` で `configure(2, rate * 40 / 1000, rate * 10 / 1000, false)` を呼ぶ。処理窓は 40 ms、更新間隔は 10 ms である。声質補正は `setFormantFactor(1, true)` と `setFormantBase(180.0f / rate)` で設定する。

Signalsmith Stretch の遅延は `inputLatency()` と `outputLatency()` の和である。v1.0.0 の検証で、処理窓 40 ms の遅延は 40 ms と測定された。

フォルマントの移動量は `setFormantSemitones(semitones, compensatePitch)` で指定できる。この関数は内部の倍率を書き換えるだけであり、`reset()` を呼ばない。

### OBS フィルター

設定キーは `semitone` だけであり、整数 -12 から +12 のスライダーである。0 半音と対応外音声では処理をバイパスし、遅延補正も 0 にする。`update()` は設定変更のたびに処理状態の初期化を要求する。

`tests/pitch-obs-probe.cpp` は遅延の期待値をフィルターの出力から取得しており、40 ms を直接記述していない。設定の検証対象は `semitone` だけである。

### 公開経路

`buildspec.json` の版は 1.0.0 である。`v*` のタグを push すると `push.yaml` が 3 OS をビルドする。下書きの Release を作成する手順は、`1.0.0` の形のタグだけを受け付ける。`v1.0.0` のタグの run では Release 作成の手順が実行されておらず、v1.0.0 の Release は手動で作成された。

ruleset `11737366` の対象は既定ブランチである。削除と非早送り更新を禁止し、1 件の承認を伴う PR を要求する。迂回できる主体は登録されていない。タグは対象外である。

v1.0.0 の公開で使用した ruleset の一時無効化、依存ソース同梱アーカイブの作成、ポータブル版での読み込み確認のスクリプトが `.deps/pitch-integration/.tmp` に残っている。

### OBS Forum

2026-11-08 の取得時点で、リソース 2338 の見出しの版は 0.1.0 である。v1.0.0 の更新 8825 は一般に表示されていない。審査後の確認を課題 `obs-simple-pitchshift-06z` で管理している。審査待ちの間に新しい更新を投稿できる状態は確認していない。

## 未確認事項

処理窓 10 ms での音質、声質補正の効果、CPU 負荷は測定していない。Claude in Chrome のブラウザー操作ツールは、本調査のセッションでは接続されていなかった。
