# Zenoh Pub/Sub over serial

Zephyr/zenoh-picoをZenoh clientとして動かし、UARTでPC上の`zenohd`へ接続する双方向Pub/Subサンプルです。

- NUCLEOのUSERボタン: LEDをトグルし、現在状態 (`on`/`off`) を`demo/nucleo/button`へpublish
- PC → NUCLEO: `demo/nucleo/led`へデータが届くたびにLEDをトグル
- UART: 115200 baud, 8-N-1, flow controlなし

## 配線とビルド

既定ターゲットはNUCLEO-C562REです。オンボードST-LINKのVirtual COM Portに接続されたUSART2 (PA2/PA3)をZenoh専用に使います。ログはUARTへ混ぜずRTTへ出します。

```sh
west build -b nucleo_c562re zenoh_serial -p always
west flash
```

別のボードでは、`boards/<board>.overlay`を追加し、Zenohに使うUARTへ`zenoh-uart` aliasを設定してください。例:

```dts
/ { aliases { zenoh-uart = &usart1; }; };
&usart1 { current-speed = <115200>; status = "okay"; };
```

UART名はdevicetreeから自動取得します。速度やkey expressionは`menuconfig`またはoverlay用confで`CONFIG_APP_ZENOH_*`を変更できます。

## PC側

ボードのVCPを確認し、シリアルをlistenする`zenohd`を起動します（ポート名は環境に合わせて変更）。

```sh
ls -l /dev/serial/by-id/
zenohd -l 'serial//dev/ttyACM0#baudrate=115200'
```

`zenohd`はルーターなので、購読とユーザー入力には別ターミナルで
zenoh-cのCLI exampleである`z_sub`/`z_pub`を使います。

```sh
# USERボタンによるpublishを購読
z_sub -k 'demo/nucleo/button'

# 実行するたび、NUCLEOのLEDが1回トグル
z_pub -k 'demo/nucleo/led' -p 'toggle'
```

シリアルデバイスを開けるプロセスは1つだけです。`screen`、シリアルモニタ、ModemManagerなどがVCPを掴んでいないことを確認してください。`zenohd`にserial transportが含まれている必要もあります。

RTTログはJ-Link RTT Viewer、または環境にあるRTT対応ツールで確認できます。ログに`Zenoh session opened`が出た後、送受信が始まります。
