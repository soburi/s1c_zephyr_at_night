# Zenoh Pub/Sub over serial

Zephyr/zenoh-picoをZenoh clientとして動かし、UARTでPC上の`zenohd`へ接続する
Pub/Subサンプルです。CAN通信は行いませんが、CAN IDと同じ番号をZenoh keyに
入れ、次の`can_zenoh`と同じ見え方を確認します。

## IDとkey

`src/main.c`のIDを対向相手と組になるように変更します。

```c
#define CAN_MESSAGE_ID_SELF   0x028 /* 自分がsubscribeして反応するID */
#define CAN_MESSAGE_ID_TARGET 0x029 /* publish先となる対向相手のID */
```

keyの形式は次のとおりです。現在のサンプルではpayloadは空で、keyに含まれる
IDを通信内容として扱います。

```text
can/<ID>/rx  ボードからpublish
can/<ID>/tx  ボードがsubscribe
```

ボタンを押すと`can/<TARGET>/rx`へpublishします。`can/<SELF>/tx`を受信すると
LEDが反転します。実装では`can/*/tx`をsubscribeし、keyからIDを取り出して
`SELF`と比較します。

## ビルドと接続

```sh
west build -b nucleo_c562re zenoh_serial -p always
west flash
zenohd -l 'serial//dev/ttyACM0#baudrate=115200'
```

NUCLEO-C562REではUSART2をZenoh専用に使い、ログはRTTへ出します。`zenohd`が使う
シリアルデバイスは、シリアルモニターなどで同時に開かないでください。

## PC側からの確認

```sh
# 全受講者のpublishを確認
z_sub -k 'can/*/rx'

# ID 0x028の装置を反応させる
z_pub -k 'can/028/tx' -p ''
```

`*`はその位置の任意の1セグメントに一致します。
