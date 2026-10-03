# SNES Controller Monitor for M5StickS3

有線SNES／スーパーファミコンコントローラーをM5StickS3に接続し、12ボタンの状態を内蔵液晶とUSBシリアルに表示するファームウェアです。PlatformIOで開発・ビルドできます。

コントローラーの入力確認や、SNESのシリアル通信を使った電子工作の作例として利用できます。

## 機能

- 十字キー、A／B／X／Y、SELECT／START、L／Rの12ボタンを読み取り
- 実機のボタン配置に合わせた液晶表示。押したボタンを緑色で表示
- 同時押しに対応
- シリアルに押下・解放イベントと現在の押下状態を出力
- 約10ms間隔で入力を読み取り、状態が変わったときに表示を更新
- シリアルには状態変更時に加え、約2秒ごとに現在の状態を出力
- シリアルモニターを開かなくても液晶表示が動作

このファームウェアの用途は入力の表示です。USB／BluetoothゲームパッドとしてPCに入力を送る機能はありません。

## 必要なもの

- M5StickS3
- 有線SNES／スーパーファミコンコントローラー（CLOCK・LATCH・DATA方式）
- 配線材、必要に応じてコントローラー用の延長ケーブルや変換コネクタ
- データ通信対応のUSBケーブル
- PlatformIOの開発環境（VS Code + PlatformIO IDE、またはPlatformIO Core）

USB接続の互換コントローラーや、スーパーファミコンミニ用コントローラーは、この配線・通信方式の対象ではありません。

## 配線（3.3V給電）

M5StickS3上部のHat2-Busを使用します。電源を切った状態で、本体の端子表記を確認して配線してください。

| コントローラーの線色（一般的な配色） | 信号 | M5StickS3の接続先 | 信号の方向 |
| --- | --- | --- | --- |
| 白 | VCC | **3V3_L2** | M5StickS3 → コントローラー |
| 茶 | GND | **GND** | 共通グランド |
| 黄 | CLOCK | **G5（GPIO5）** | M5StickS3 → コントローラー |
| 橙 | LATCH | **G6（GPIO6）** | M5StickS3 → コントローラー |
| 赤 | DATA | **G7（GPIO7）** | コントローラー → M5StickS3 |

GPIO番号はコネクタの物理的なピン番号とは異なります。互換品や交換ケーブルでは線色が異なる場合があるため、色だけで判断せず、コネクタや基板との導通を確認してください。

### 電源・信号電圧

SNESコントローラーの本来の電源は5Vです。このプロジェクトでは3.3Vで給電しますが、すべての純正品・互換品での動作を保証するものではありません。ファームウェアは初期化時に3.3V LDO出力を有効にします。

**M5StickS3のGPIOに5V信号を直接入力しないでください。** コントローラーを5Vで動作させる場合は、CLOCK／LATCHを3.3V→5V、DATAを5V→3.3Vへ変換する適切なレベル変換回路が必要です。白線だけを5V端子へ変更して、DATAをGPIOへ直結する配線は避けてください。

## ビルド・書き込み

M5StickS3をUSB接続し、PlatformIOでプロジェクトのルートフォルダーから実行します。シリアルモニターは**115200bps**です。

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

## 液晶表示

横向きの画面に、コントローラーの形とボタンを表示します。

```text
        L                              R

        ↑                              X
     ←  ＋  →     SELECT  START      Y     A
        ↓                              B
```

押していないボタンは灰色、押しているボタンは緑色になります。下部には`NO BUTTON PRESSED`または`GREEN = PRESSED`を表示します。

## シリアル出力

出力例です。時刻は起動後の経過時間（ミリ秒）です。

```text
[1000 ms] B PRESSED
[1000 ms] STATE: B (mask=0x001)
[1200 ms] A PRESSED
[1200 ms] STATE: B A (mask=0x101)
[1500 ms] B RELEASED
[1500 ms] STATE: A (mask=0x100)
[1700 ms] A RELEASED
[1700 ms] STATE: NONE (mask=0x000)
```

`mask`は押下状態を表す12ビットの値です。下位ビットから順に、B、Y、SELECT、START、UP、DOWN、LEFT、RIGHT、A、X、L、Rが対応し、`1`が押下を表します。

## プロジェクト構成

| ファイル | 内容 |
| --- | --- |
| `src/main.cpp` | コントローラー読み取り、液晶描画、シリアル出力 |
| `platformio.ini` | ボード、Arduinoフレームワーク、依存ライブラリの設定 |
| `.gitignore` | ビルド生成物などのGit管理対象からの除外 |

GPIOを変更する場合は、`src/main.cpp`の`kClockPin`、`kLatchPin`、`kDataPin`を変更してください。

依存ライブラリはM5UnifiedとM5PM1です。現在の`platformio.ini`はGitHub URLで取得する構成で、コミットやタグを固定していないため、取得時期によってライブラリのバージョンが変わる可能性があります。

## 動作確認状況・制限

- M5StickS3向けのPlatformIOビルドと、USB経由の書き込みを確認済み
- 開発時の3.3V配線で、初期の入力表示について動作確認の報告あり
- 使用コントローラーの型番や、複数モデルでの互換性確認は未記録
- 未接続と「どのボタンも押されていない状態」は区別しません。未接続時にも`NONE`と表示される場合があります

## 参考資料

- [M5Stack公式：StickS3仕様・ピンマップ](https://docs.m5stack.com/en/core/StickS3)
- [raphnet：SNESコントローラーの配線色（8ページ）](https://www.raphnet-tech.com/downloads/M_NSDB9_PCB_ENG3.pdf)
- [Espressif：ESP32-S3データシート](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [M5Unified](https://github.com/m5stack/M5Unified)
- [M5PM1](https://github.com/m5stack/M5PM1)
