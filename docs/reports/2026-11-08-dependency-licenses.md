# v1.0.0の追加依存ライセンス確認

2026-11-08に、採用コミットのライセンス本文、CMakeの依存関係、最終配布ZIPとソースアーカイブの同梱ファイルを確認した。

## 結論

Signalsmith StretchとSignalsmith Linearの採用を妨げるライセンス条件は見つからなかった。両者はMITライセンスであり、本体のGPLv2と両立する。著作権表示とライセンス本文を配布物へ同梱した。ライセンス料、商用利用の禁止、個別許諾の要件は本文にない。

| 依存 | 採用コミット | ライセンス | 著作権表示 |
|---|---|---|---|
| Signalsmith Stretch | `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da` | MIT | 2022 Geraint Luff / Signalsmith Audio Ltd. |
| Signalsmith Linear | `7bb1c8f36794d6980080ce817c91f8da8fe56a66` | MIT | 2025 Signalsmith Audio |

## 確認範囲

StretchはLinearに依存する。Windows候補のCMake設定ではIPP、PFFFT、XSIMDを無効にしており、CMSIS-DSPも有効化していない。追加したライブラリのソースへ独自の変更を加えていない。macOSのCMake設定にはOSのAccelerate frameworkを使用する分岐があり、framework本体を配布物へコピーする処理はない。

CI run `42420765811`のWindows ZIPに含まれる2つのMIT本文を取得元とSHA256で照合し、完全一致を確認した。

| ファイル | SHA256 |
|---|---|
| `signalsmith-stretch-LICENSE.txt` | `16BF62D475AF149D96A24853E77C9E0D939259A9D55B23B41DA7F9939DB85085` |
| `signalsmith-linear-LICENSE.txt` | `A91F5703B0F5FA22E8C14A8BD854559A7C512BBE58656EA8BDCD4964E923AB2F` |

## 公開物の確認

最終Windows ZIPの`data/licenses`にMIT本文2件と本体のGPL本文を同梱し、3件とも原本との一致を確認した。[v1.0.0 Release](https://github.com/AllegroMoltoV/obs-simple-pitchshift/releases/tag/v1.0.0)にWindows ZIPと`simple-pitchshift-1.0.0-source-with-dependencies.tar.gz`を公開した。

GitHubが自動生成するソースZIPだけでは、FetchContentで取得する両ライブラリの実体を含まない。追加のソースアーカイブに最終タグの製品ソースとビルド設定、両ライブラリの固定コミットのソース、各ライセンス、固定revisionのmanifest、同梱ソースを使うビルド案内を収録した。タグと依存コミットのGitアーカイブに対して全ファイルのバイト列とシンボリックリンクを照合した。StretchのCLIサンプル専用`cmd/util`サブモジュールを含めていない。製品のCMakeで取得・使用しない対象であり、除外理由を配布物に記載した。

公開後の再取得でも両配布物のSHA256が検証済みファイルと一致した。ハッシュと公開状態を[公開検証報告](2026-11-08-v100-publish.md)に記録した。本調査はライセンス本文と採用構成を対象とし、第三者特許の網羅的な調査は含まない。

## 一次資料

- [Stretchの採用コミットのMIT本文](https://github.com/Signalsmith-Audio/signalsmith-stretch/blob/88061fd4eda56a2353c5d65c74aa3f4ccf76f9da/LICENSE.txt)
- [Linearの採用コミットのMIT本文](https://github.com/Signalsmith-Audio/linear/blob/7bb1c8f36794d6980080ce817c91f8da8fe56a66/LICENSE.txt)
- [GNUによるExpat/MITとGPLの互換性説明](https://www.gnu.org/licenses/license-list.html#Expat)
- [GPLv2本文、第1〜3項](https://opensource.org/license/gpl-2.0)
