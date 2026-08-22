SWEST28: s1c - 夜のZephyr RTOSハンズオン: STMicroelectronics Nucleo-C562REで学ぶZephyr入門 
==========================================================================================

準備
----

セットアップを途中からやり直す場合は、ホームディレクトリに作成された
`swest28`と`zephyr-sdk-1.0.1`を削除してから、セットアップスクリプトを再実行してください。
必要なファイルがある場合は、削除する前に退避してください。

### Ubuntu/WSL2(おすすめ)

#### セットアップ

ターミナルで以下を実行してください。

```
curl -LO https://raw.githubusercontent.com/soburi/s1c_zephyr_at_night/refs/heads/main/tools/swest28_setup.sh
bash swest28_setup.sh
```

別のターミナルでログインして、以下のコマンドを実行します。

```
cd swest28
source .venv/bin/activate
west build -p -b nucleo_c562re zephyr/samples/basic/blinky
```

ビルドのログの最後に以下のようなメッセージが出れば成功です。

```
[161/161] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:       18304 B       512 KB      3.49%
             RAM:        4328 B       128 KB      3.30%
        IDT_LIST:           0 B        32 KB      0.00%
Generating files from /home/crs/swest28/build/zephyr/zephyr.elf for board: nucleo_c562re/stm32c562xx
```

続けて、デバッガの確認を以下のコマンドで行います。

```
pyocd list --targets | grep stm32c562re
```

以下のようなメッセージが出ればOKです。

```
  stm32c562re               STMicroelectronics       STM32C562RE                  STM32C5 Series, STM32C55x/562   pack
  stm32c562ret3             STMicroelectronics       STM32C562RET3                STM32C5 Series, STM32C55x/562   pack
  stm32c562ret3tr           STMicroelectronics       STM32C562RET3TR              STM32C5 Series, STM32C55x/562   pack
  stm32c562ret6             STMicroelectronics       STM32C562RET6                STM32C5 Series, STM32C55x/562   pack
  stm32c562ret6j            STMicroelectronics       STM32C562RET6J               STM32C5 Series, STM32C55x/562   pack
```


ツールの起動確認をします。別のターミナルを開いて以下のコマンドを実行します。


```
swest28/s1c_zephyr_at_night/tools/zenohd  -l "tcp/127.0.0.1:7447"
```

エラーなど発生せず、以下のログで待ち受けていれば成功です。

```
2026-08-23T22:22:57.528308Z  INFO main ThreadId(01) zenoh::net::runtime::orchestrator: Zenoh can be reached at: tcp/[240b:10:2f01:2600:6df0:fc2f:13f9:6de4]:7447
2026-08-23T22:22:57.528366Z  INFO main ThreadId(01) zenoh::net::runtime::orchestrator: Zenoh can be reached at: tcp/[fe80::d893:daa7:a70d:2b89]:7447
2026-08-23T22:22:57.528393Z  INFO main ThreadId(01) zenoh::net::runtime::orchestrator: Zenoh can be reached at: tcp/10.255.255.254:7447
2026-08-23T22:22:57.528399Z  INFO main ThreadId(01) zenoh::net::runtime::orchestrator: Zenoh can be reached at: tcp/192.168.0.209:7447
2026-08-23T22:22:57.539451Z  INFO main ThreadId(01) zenoh::net::runtime::orchestrator: Listening scout messages on 224.0.0.224:7446
```

pythonツールの起動確認をします。** 元のターミナルで ** 以下のコマンドを実行します。

```
python3 s1c_zephyr_at_night/tools/z_pub.py
```

以下のように表示されればOKです。

```
TX key=can/028/tx, payload=746f67676c65, length=6
```


### Windowsネイティブ

コマンドプロンプトで以下を実行してください。

```
curl.exe -LO https://raw.githubusercontent.com/soburi/s1c_zephyr_at_night/refs/heads/main/tools/swest28_setup.bat
swest28_setup.bat
```


WSL2を使う場合、以下のツールをインストールすると便利です。
https://gitlab.com/alelec/wsl-usb-gui/-/releases/v5.8.0


別のコマンドプロンプトを開いて、以下のコマンドを実行します。

```
cd swest28
.venv/Scripts/activate.bat
west build -p -b nucleo_c562re zephyr/samples/basic/blinky
```

ビルドのログの最後に以下のようなメッセージが出れば成功です。

```
[161/161] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:       18304 B       512 KB      3.49%
             RAM:        4328 B       128 KB      3.30%
        IDT_LIST:           0 B        32 KB      0.00%
Generating files from /home/crs/swest28/build/zephyr/zephyr.elf for board: nucleo_c562re/stm32c562xx
```

続けて、デバッガの確認を以下のコマンドで行います。

```
pyocd list --targets > pyocd.targets.txt
findstr stm32c562re pyocd.targets.txt
```

以下のようなメッセージが出ればOKです。

```
  stm32c562re               STMicroelectronics       STM32C562RE                  STM32C5 Series, STM32C55x/562   pack
  stm32c562ret3             STMicroelectronics       STM32C562RET3                STM32C5 Series, STM32C55x/562   pack
  stm32c562ret3tr           STMicroelectronics       STM32C562RET3TR              STM32C5 Series, STM32C55x/562   pack
  stm32c562ret6             STMicroelectronics       STM32C562RET6                STM32C5 Series, STM32C55x/562   pack
  stm32c562ret6j            STMicroelectronics       STM32C562RET6J               STM32C5 Series, STM32C55x/562   pack
```


ツールの起動確認をします。** 別のコマンドプロンプトを開いて ** 以下のコマンドを実行します。


```
swest28\s1c_zephyr_at_night\tools\zenohd  -l "tcp/127.0.0.1:7447"
```

エラーなど発生せず、以下のログで待ち受けていれば成功です。

```
2026-08-23T22:22:57.528308Z  INFO main ThreadId(01) zenoh::net::runtime::orchestrator: Zenoh can be reached at: tcp/[240b:10:2f01:2600:6df0:fc2f:13f9:6de4]:7447
2026-08-23T22:22:57.528366Z  INFO main ThreadId(01) zenoh::net::runtime::orchestrator: Zenoh can be reached at: tcp/[fe80::d893:daa7:a70d:2b89]:7447
2026-08-23T22:22:57.528393Z  INFO main ThreadId(01) zenoh::net::runtime::orchestrator: Zenoh can be reached at: tcp/10.255.255.254:7447
2026-08-23T22:22:57.528399Z  INFO main ThreadId(01) zenoh::net::runtime::orchestrator: Zenoh can be reached at: tcp/192.168.0.209:7447
2026-08-23T22:22:57.539451Z  INFO main ThreadId(01) zenoh::net::runtime::orchestrator: Listening scout messages on 224.0.0.224:7446
```

pythonツールの起動確認をします。** 元のコマンドプロンプトで ** 以下のコマンドを実行します。

```
python s1c_zephyr_at_night\tools\z_pub.py
```

以下のように表示されればOKです。

```
TX key=can/028/tx, payload=746f67676c65, length=6
```
