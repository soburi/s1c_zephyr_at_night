# CAN–Zenoh gateway over serial

CANとZenohを双方向に中継するZephyrアプリケーションです。Zenoh clientとして
UARTでPC上の`zenohd`へ接続します。

- CAN → Zenoh: 任意の標準CAN IDを受信し、そのデータを
  `<prefix>/<CAN ID>/rx`へ生バイト列としてpublish
- Zenoh → CAN: `<prefix>/*/tx`をsubscribeし、トピック中のIDをCAN IDとして
  受信した生バイト列を送信
- LED: CANフレームをZenohへ正常にpublishするたびにトグル

既定prefixは`can`、IDは`0x`なしの3桁小文字16進数です。方向別の`rx`と`tx`を
使うため、ゲートウェイ自身のpublishをsubscribeして折り返すことはありません。

## ビルド

```sh
west build -b nucleo_c562re can_zenoh -p always
west flash
```

NUCLEO-C562REではUSART2をZenoh専用に使い、ログはRTTへ出します。CANトランシーバ、
CANH/CANL、共通GND、終端抵抗も接続してください。

## PC側

```sh
zenohd -l 'serial//dev/ttyACM0#baudrate=115200'

# 全CAN IDで受信したデータを表示
z_sub -k 'can/*/rx'

# ASCIIの "hello" をCAN ID 0x101、DLC 5で送信
z_pub -k 'can/101/tx' -p 'hello'
```

Classic CANを使用するため、Zenohから送れるpayloadは0〜8 byteです。それより長い
payloadや、3桁16進数の標準CAN IDとして解釈できないトピックは破棄します。
prefixは`CONFIG_APP_ZENOH_KEY_PREFIX`で変更できます。
