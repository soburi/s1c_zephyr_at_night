# Zenoh Pub/Sub over serial

Zephyr/zenoh-picoをZenoh clientとして動かし、UARTでPC上の`zenohd`へ接続する双方向Pub/Subサンプルです。

- ボード → PC: `demo/zephyr/tx`へ1秒ごとにpublish
- PC → ボード: `demo/zephyr/rx`をsubscribe（受信内容はRTTログへ表示）
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

別ターミナルから確認します。`z_sub`/`z_pub`はzenoh-cのCLI examplesです。

```sh
# ボードからのpublishを受信
z_sub -k 'demo/zephyr/tx'

# ボードのsubscriberへ送信
z_pub -k 'demo/zephyr/rx' -p 'hello from PC'
```

シリアルデバイスを開けるプロセスは1つだけです。`screen`、シリアルモニタ、ModemManagerなどがVCPを掴んでいないことを確認してください。`zenohd`にserial transportが含まれている必要もあります。

RTTログはJ-Link RTT Viewer、または環境にあるRTT対応ツールで確認できます。ログに`Zenoh session opened`が出た後、送受信が始まります。
