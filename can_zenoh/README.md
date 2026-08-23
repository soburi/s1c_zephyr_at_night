# CAN–Zenoh gateway over serial

CANとZenohを双方向に中継する最終サンプルです。複数の装置を同一CANバスへ
接続し、CAN IDとZenoh keyがどのように見えるかを確認します。

## 中継ルール

```text
CAN ID 0x028 を受信    -> can/028/rx へpublish
can/028/tx をsubscribe -> CAN ID 0x028 を送信
```

方向を`rx`と`tx`で分けることで、ゲートウェイ自身のpublishがそのまま
subscribeされることを防ぎます。現在のサンプルはIDの見え方に焦点を当てるため、
CANフレームはDLC 0、Zenoh payloadは空です。

CAN側では全IDを受信し、Zenoh側では`can/*/tx`をsubscribeします。

## IDの設定

```c
#define CAN_MESSAGE_ID_SELF   0x028 /* 自分のID */
#define CAN_MESSAGE_ID_TARGET 0x029 /* 対向相手のID */
```

ボタンを押すと、対向相手のIDをCANとZenohの両方へ送ります。

## CANバスの配線と終端

全装置のCANH、CANL、GNDを共通に接続します。分岐線はできるだけ短くします。
終端抵抗120 ohmは装置ごとではなく、バス配線の物理的な両端に1個ずつ接続します。
電源OFF時にCANH–CANL間を測定すると、並列合成により約60 ohmになります。
短い配線や低速では終端が不適切でも動く場合がありますが、安定動作が保証されるわけではありません。

## ビルドと接続

```sh
west build -b nucleo_c562re can_zenoh -p always
west flash
zenohd -l 'serial//dev/ttyACM0#baudrate=115200'
```

NUCLEO-C562REではUSART2をZenoh専用に使い、ログはRTTへ出します。

## PC側からの確認

```sh
# CANバス上で受信された全IDを確認
z_sub -k 'can/*/rx'

# CAN ID 0x028のフレームをバスへ送信
z_pub -k 'can/028/tx' -p ''
```

各受講者のIDに変えてpublishし、どの装置が反応するか、また
`can/*/rx`から複数装置の通信がどのように見えるかを確認します。
