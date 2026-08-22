Button and LED
==============

ボードのUSERボタンとLEDを使い、GPIOの入出力と割込みを確認する
サンプルです。ボタンの状態を1 ms周期で読み取ってLEDへ反映し、押下時には
GPIO割込みでメッセージを表示します。

学習項目
--------

* Devicetreeの ``sw0`` と ``led0`` alias
* GPIOの入力・出力設定
* ボタン状態のポーリング
* GPIO割込みとcallback

ビルド
------

.. code-block:: console

   west build -b nucleo_c562re button -p always
   west flash

確認
----

ボタンを押している間LEDが点灯し、押下時にRTTログへ
``Button pressed`` が表示されることを確認します。
