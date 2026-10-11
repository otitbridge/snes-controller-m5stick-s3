# SNES Controller Monitor for M5StickS3

有線SNES／スーパーファミコンコントローラーをM5StickS3に接続し、12ボタンの状態を内蔵液晶、USBシリアル、Webブラウザーに表示するファームウェアです。PlatformIOで開発・ビルドできます。

コントローラーの入力確認や、SNESのシリアル通信を使った電子工作の作例として利用できます。

## 動作映像

[![SNESコントローラー、M5StickS3の液晶、スマートフォンのWebモニターで左キーの押下状態を表示](docs/images/snes-controller-web-monitor.png)](https://x.com/ot_it_bridge/status/2106715352856203522)

実機コントローラーの操作を、M5StickS3の液晶とスマートフォンのWebモニターに表示している様子です。

[実機での動作映像を見る（X：@ot_it_bridge）](https://x.com/ot_it_bridge/status/2106715352856203522)

## 機能

- 十字キー、A／B／X／Y、SELECT／START、L／Rの12ボタンを読み取り
- 実機のボタン配置に合わせた液晶表示。押したボタンを緑色で表示
- 同時押しに対応
- USB HIDキーボードとしてmGBAなどのエミュレーターを操作
- USBシリアルに押下・解放イベントと現在の押下状態を出力
- 約10ms間隔で入力を読み取り、状態が変わったときに表示を更新
- シリアルには状態変更時に加え、約2秒ごとに現在の状態を出力
- シリアルモニターを開かなくても液晶表示が動作
- M5StickS3がWi-Fi親機として動作し、最大2台の端末からWebモニターに接続
- WebSocketでボタンの変化を送信。接続時の状態同期、切断表示、自動再接続に対応
- ブラウザーで遊べる横スクロールゲーム「Coin Trail」。SNES操作で移動・ジャンプし、コイン取得時に効果音を再生

USB接続時はCDCシリアルとHIDキーボードの複合デバイスとして動作します。ゲームパッドではなくキーボード入力としてPCへ送信します。

### USB HIDキー割り当て

| SNES操作 | USBキーボード | mGBA既定操作 |
| --- | --- | --- |
| 十字キー | 矢印キー | 十字キー |
| B | Z | Game Boy B |
| A | X | Game Boy A |
| SELECT | Backspace | SELECT |
| START | Enter | START |

SNESのX／Y／L／Rは未割り当てです。ブラウザー上のnoVNCで使用する場合は、接続したゲーム画面をクリックしてキーボードフォーカスを与えてください。

## 必要なもの

- M5StickS3
- 有線SNES／スーパーファミコンコントローラー（CLOCK・LATCH・DATA方式）
- 配線材、必要に応じてコントローラー用の延長ケーブルや変換コネクタ
- データ通信対応のUSBケーブル
- PlatformIOの開発環境（VS Code + PlatformIO IDE、またはPlatformIO Core）

USB接続の互換コントローラーや、スーパーファミコンミニ用コントローラーは、この配線・通信方式の対象ではありません。

## 機器構成図

M5StickS3に有線コントローラーを接続し、スマートフォンやPCはM5StickS3のWi-Fiに直接接続します。無線ルーターやインターネット接続は不要です。

```mermaid
flowchart LR
    Controller["SNES / スーパーファミコン<br/>有線コントローラー"]
    Stick["M5StickS3<br/>内蔵液晶・Wi-Fi親機<br/>192.168.4.1"]
    Browser["スマートフォン / PC<br/>ブラウザー・最大2台"]
    DevPC["開発用PC<br/>PlatformIO / シリアルモニター"]

    Stick -->|"白：3.3V / 黄：CLOCK / 橙：LATCH"| Controller
    Controller -->|"赤：DATA"| Stick
    Stick ---|"茶：共通GND"| Controller
    Stick <-->|"Wi-Fi：SNES-Monitor"| Browser
    DevPC <-->|"USB：書き込み・シリアル出力"| Stick
```

USBはファームウェアの書き込みとシリアルモニターの利用に必要です。液晶・Web表示・ゲームの利用時は、本体のバッテリーでも動作します。線色は以下の配線表と同じ作例のものです。

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

## 処理ブロック図

入力読み取り・液晶表示と、Web通信は別のタスクで処理します。ゲームの描画・進行・効果音はアクセスした端末のブラウザーで実行します。

```mermaid
flowchart TD
    Controller["SNESコントローラー"]
    subgraph MCU["M5StickS3"]
        Input["入力読み取り・表示タスク<br/>約10msごとに16ビット読み取り<br/>12ボタンの押下状態を取得"]
        LCD["内蔵液晶<br/>押下ボタンを表示"]
        Serial["USBシリアル出力<br/>押下・解放イベント / 現在状態"]
        States["状態共有<br/>最新状態 + 送信待ちキュー（最大64件）"]
        Network["Web通信タスク<br/>HTTP：80 / WebSocket：81"]
        Input --> LCD
        Input --> Serial
        Input -->|"状態変化"| States
        States --> Network
    end
    subgraph Client["端末のブラウザー"]
        Page["HTML・JavaScript<br/>モニター / またはゲーム"]
        Buttons["Webモニター<br/>押下状態を表示"]
        Game["Coin Trail<br/>移動・ジャンプ・コイン獲得"]
        Sound["端末のスピーカー<br/>コイン獲得の効果音"]
        Page --> Buttons
        Page --> Game
        Game --> Sound
    end
    Controller -->|"DATA（GPIO7）"| Input
    Input -->|"CLOCK（GPIO5）/ LATCH（GPIO6）"| Controller
    Network -->|"HTTP：画面を配信"| Page
    Network -->|"WebSocket：状態変化 + 1秒ごとの現在状態"| Page
```

各端末はモニターまたはゲームのどちらかを開き、合計2画面まで接続できます。ゲームの進行とコイン数は端末ごとに独立しています。

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

押していないボタンは灰色、押しているボタンは緑色になります。上部にWi-Fi名とアクセス先、下部に`NO BUTTON PRESSED`または`GREEN = PRESSED`を表示します。Wi-Fiの初期化に失敗した場合も液晶とシリアルの表示は継続します。

## Webモニター

1. スマートフォンやPCをWi-Fi **`SNES-Monitor`** に接続します。パスワードは **`12345678`** です。
2. ブラウザーで **`http://192.168.4.1/`** を開きます。
3. 「接続済み」と表示されたら、コントローラーを操作します。液晶と同じボタン配置で押下状態を表示します。

Wi-Fiの同時接続は最大2台、WebSocket接続も最大2画面です。各端末で1画面ずつ開いてください。画面上のボタンは表示専用です。

インターネット接続は不要です。端末に「インターネット接続なし」と表示されても、このWi-Fiへの接続を維持してください。HTTPSではなくHTTPでアクセスします。

切断時は押下表示を消して状態不明と表示し、自動再接続します。再接続時には全ボタンの現在状態を同期します。状態変更時の送信に加え、1秒ごとに現在状態を送信し、約5秒間受信できない場合は再接続を試みます。

入力読み取りと液晶表示は通信処理とは別のタスクで実行します。通信が滞った場合に送信待ちが無制限に増えないよう、保留状態は最大64件とし、上限に達すると古いものから破棄します。通信混雑時の短い操作はWeb側で取りこぼす場合があります。

## 横スクロールゲーム「Coin Trail」

同じWi-Fiに接続し、**`http://192.168.4.1/game`** を開きます。モニター画面の「横スクロールゲームで遊ぶ」からも移動できます。

最初に画面の **「プレイ・音を有効にする」** を押してください。コインを取ると、アクセスしている端末のスピーカーから効果音が鳴ります。音は画面のボタンでOFFにできます。

| SNES操作 | 動作 |
| --- | --- |
| 十字キー左右 | 左右移動 |
| B / A | ジャンプ。短く押すと低く、長く押すと高く跳ぶ |
| Y + 左右 | ダッシュ |
| START | 一時停止 / 再開 |
| SELECT | コイン数とコースをリセット |

足場を跳び渡り、コインを集めるゲームです。モンスターとゴール旗はありません。穴に落ちると近くの地面から復帰し、集めたコインは維持します。コースの端まで進むと次の周回になり、コインが再配置されます。キャラクター、背景、効果音はコードで生成し、画像・音声ファイルや外部配信サービスは使用しません。

PCでは矢印キー、Space / Z（ジャンプ）、Shift（ダッシュ）、Enter（一時停止 / 再開）でも操作できます。タブが非表示になると一時停止します。Wi-Fi切断時はSNES入力を解除し、再接続を試みます。

各端末のゲーム進行とコイン数は独立しています。同じSNESコントローラーの入力が両方に届きます。モニターとゲームはそれぞれ1つのWebSocket接続を使うため、合計2画面までにしてください。ゲームの状態はページを閉じると失われます。

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
| `src/web_monitor.cpp` / `src/web_monitor.h` | Wi-Fi親機、HTTP配信、WebSocketによる状態送信 |
| `src/web_page.h` | 外部通信不要のWebモニター画面 |
| `src/game_page.h` | 横スクロールゲーム、操作処理、コイン効果音 |
| `platformio.ini` | ボード、Arduinoフレームワーク、依存ライブラリの設定 |
| `.gitignore` | ビルド生成物などのGit管理対象からの除外 |
| `LICENSE` | 本プロジェクトのMIT License |

GPIOを変更する場合は、`src/main.cpp`の`kClockPin`、`kLatchPin`、`kDataPin`を変更してください。

Wi-Fi名とパスワードは`src/web_monitor.h`で設定できます。

直接指定する依存ライブラリは、動作確認済みの以下のバージョンに固定しています。

| ライブラリ | 固定先 |
| --- | --- |
| M5Unified | `fd40d58b8405ad1e7ed7afab548dab5e7cec5bbb` |
| M5PM1 | `be9a5456c007c333e7ac963f33bfde1ffa5d82ee` |
| WebSockets | `2.7.2` |

M5UnifiedとM5PM1はGitHubから指定コミットを取得します。これらが間接的に取得する依存ライブラリのすべてを固定する構成ではありません。

## ライセンス

本プロジェクトのソースコードおよび付属ドキュメントは[MIT License](LICENSE)で公開します。著作権表示は `Copyright (c) 2026 otitbridge` です。

改変・再配布・商用利用を認め、本プロジェクトを利用した機器の製作・販売を含む商用利用を歓迎します。再配布するコードやドキュメントには著作権表示とライセンス文を保持してください。MIT Licenseは改変コードの公開を義務付けません。

依存ライブラリには、それぞれのライセンスが適用されます。本プロジェクトのMIT Licenseによって変更されるものではありません。

| 直接指定する依存ライブラリ | ライセンス |
| --- | --- |
| M5Unified | [MIT](https://github.com/m5stack/M5Unified/blob/fd40d58b8405ad1e7ed7afab548dab5e7cec5bbb/LICENSE) |
| M5PM1 | [MIT](https://github.com/m5stack/M5PM1/blob/be9a5456c007c333e7ac963f33bfde1ffa5d82ee/LICENSE) |
| WebSockets | [LGPL-2.1](https://github.com/Links2004/arduinoWebSockets/blob/2.7.2/LICENSE) |

依存ライブラリを含むファームウェアや製品を配布する場合は、間接依存を含め各ライブラリのライセンス条件にも従ってください。M5StickS3やSNESコントローラーなど既存製品の設計・商標は、本プロジェクトのライセンス対象ではありません。

## 動作確認状況・制限

- M5StickS3向けのPlatformIOビルドと、USB経由の書き込みを確認済み
- 開発時の3.3V配線で、初期の入力表示について動作確認の報告あり
- 使用コントローラーの型番や、複数モデルでの互換性確認は未記録
- Webモニターの2台同時接続、実際の応答時間は実機での確認が必要
- 未接続と「どのボタンも押されていない状態」は区別しません。未接続時にも`NONE`と表示される場合があります

## 参考資料

- [M5Stack公式：StickS3仕様・ピンマップ](https://docs.m5stack.com/en/core/StickS3)
- [raphnet：SNESコントローラーの配線色（8ページ）](https://www.raphnet-tech.com/downloads/M_NSDB9_PCB_ENG3.pdf)
- [Espressif：ESP32-S3データシート](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [M5Unified](https://github.com/m5stack/M5Unified)
- [M5PM1](https://github.com/m5stack/M5PM1)
- [arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets)
