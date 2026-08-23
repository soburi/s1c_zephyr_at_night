CAN peer communication
======================

ボタンを押すと対向相手のCAN IDへフレームを送信します。同一CANバス上の
全IDを受信・表示し、自分のIDと一致したときだけLEDを反転します。

CAN IDの設定
-------------

``src/main.c`` の2つのIDを、対向相手と組になるように変更します。

.. code-block:: c

   #define CAN_MESSAGE_ID_SELF   0x028 /* 自分が反応するID */
   #define CAN_MESSAGE_ID_TARGET 0x029 /* 対向相手が反応するID */

Aさんの ``SELF`` が ``0x028``、Bさんの ``SELF`` が ``0x029`` なら、Aさんの
``TARGET`` は ``0x029``、Bさんの ``TARGET`` は ``0x028`` です。

配線
----

2台のCANH、CANL、GNDを共通に接続します。CANトランシーバが必要です。
終端抵抗120 ohmは装置ごとではなく、バス配線の物理的な両端に接続します。

ビルドと確認
----------

.. code-block:: console

   west build -b nucleo_c562re can_comm -p always
   west flash

* ボタンを押した装置が ``TARGET`` IDで送信する
* 対向装置が受信IDを表示し、``SELF`` と一致するとLEDを反転する
* 同じバス上の他のIDもログに表示されるがLEDは反応しない
