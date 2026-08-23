/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NOTE: If you are looking into an implementation of button events with
 * debouncing, check out `input` subsystem and `samples/subsys/input/input_dump`
 * example instead.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>

#define SLEEP_TIME_MS	1
#define CAN_MESSAGE_ID_SELF   0x28
#define CAN_MESSAGE_ID_TARGET 0x28
#define CAN_RX_QUEUE_SIZE     16

K_MSGQ_DEFINE(can_rx_queue, sizeof(uint32_t), CAN_RX_QUEUE_SIZE, sizeof(uint32_t));

/*
 * デバイスツリーのsw0 のエイリアスをボタンとして使う。必須。
 */
#define SW0_NODE	DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;
static struct k_work button_work;
static struct k_work can_rx_work;

/**
 * デバイスツリーで led0のエイリアスが定義されていればそれを使う。オプション。
 */
static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,
						     {0});

static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

/**
 * LEDを反転して状態を取得する
 * @param led 操作対象のGPIO
 * @return LEDの状態
 */
static bool toggle_led(struct gpio_dt_spec *led)
{
	int led_state = 0;

	if (gpio_pin_get_dt(led) == 0) {
		led_state = 1;
	} else {
		led_state = 0;
	}

	(void)gpio_pin_set_dt(led, led_state);
	return led_state;
}

/**
 * 1バイトのcanメッセージを送信
 * @param canid CAN ID
 */
void send_status_can_msg(uint32_t canid)
{
	struct can_frame frame = {0};
	int ret;

	frame.id = canid;
	frame.dlc = 0;

	ret = can_send(can_dev, &frame, K_NO_WAIT, NULL, NULL);
	if (ret != 0) {
		printk("CAN send failed for 0x%03x (%d)\n", frame.id, ret);
		return;
	}
	printk("CAN message sent: 0x%03x (%u bytes)\n", frame.id, frame.dlc);
}

/**
 * CANメッセージ受信時に遅延実行で行う処理
 * キューに入れたデータを取得して、自分のIDが指定されていたら
 * LEDを反転する。
 */
static void can_rx_work_handler(struct k_work *work)
{
	uint32_t received_id;

	ARG_UNUSED(work);

	while (k_msgq_get(&can_rx_queue, &received_id, K_NO_WAIT) == 0) {
		if (led.port) {
			if (received_id == CAN_MESSAGE_ID_SELF) {
				(void)toggle_led(&led);
			}
		}

		printk("CAN message received with ID 0x%03x\n", received_id);
	}
}

/**
 * CANメッセージを受け取ったときの動作
 * idの情報をキューに入れる
 */
static void can_received(const struct device *dev, struct can_frame *frame,
			 void *user_data)
{
	uint32_t id = frame->id;

	if (k_msgq_put(&can_rx_queue, &id, K_NO_WAIT) != 0) {
		return;
	}

	(void)k_work_submit(&can_rx_work);
}

/**
 * ボタン押下時に遅延実行で行う処理.
 * LEDの反転とCANメッセージの送信を行う
 */
static void button_work_handler(struct k_work *work)
{
	bool enabled = 0;

	if (led.port) {
		enabled = toggle_led(&led);
	}

	send_status_can_msg(CAN_MESSAGE_ID_TARGET);
}

/**
 * ボタン押下時の処理
 * ログ出力とボタン押下時処理の登録を行う.
 * 割込みのコールバックで時間のかかるCAN送信処理は行えない。
 */
void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
	(void)k_work_submit(&button_work);
}

/*
 * main
 */
int main(void)
{
	int ret;

	/* CANデバイスのチェック */
	if (!device_is_ready(can_dev)) {
		printk("CAN device is not ready\n");
		return 0;
	}

	const struct can_filter filter = {
		.id = 0,
		.mask = 0,
	};

	/* CANメッセージ受信時に実行するwork の初期化. */
	k_work_init(&can_rx_work, can_rx_work_handler);

	/* 全CAN IDを受信するフィルタを設定 */
	ret = can_add_rx_filter(can_dev, can_received, NULL, &filter);
	if (ret < 0) {
		printk("CAN receive filter registration failed (%d)\n", ret);
		return 0;
	}

	/* !!!!! CAN通信開始 !!!!! */
	ret = can_start(can_dev);
	if (ret != 0) {
		printk("CAN start failed (%d)\n", ret);
		return 0;
	}

	/* ボタンが利用可能かのチェック */
	if (!gpio_is_ready_dt(&button)) {
		printk("Error: button device %s is not ready\n",
		       button.port->name);
		return 0;
	}

	/* ボタンの接続されているGPIOピンを入力モードにする */
	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button.port->name, button.pin);
		return 0;
	}

	/* ボタンの接続されているGPIOピンのエッジ割込み(L->H, H->L時) を有効にする */
	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button.port->name, button.pin);
		return 0;
	}

	/* ボタン押下時に実行するwork の初期化. */
	k_work_init(&button_work, button_work_handler);
	/* 割込み発生時に button_pressed が呼ばれるように登録 */
	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	printk("Set up button at %s pin %d\n", button.port->name, button.pin);

	/* LEDのGPIOが有効かの確認 */
	if (led.port && !gpio_is_ready_dt(&led)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",
		       ret, led.port->name);
		led.port = NULL;
	}
	if (led.port) {
		/* LEDが有効の場合、そのGPIOピンを出力モードに設定 */
		ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, led.port->name, led.pin);
			led.port = NULL;
		} else {
			printk("Set up LED at %s pin %d\n", led.port->name, led.pin);
		}
	}

	printk("Press the button\n");

	/* CAN受信処理でLED状態を変化させるので、LED状態を変化させるループは削除。
	 * 無期限の待ちに入る
	 */
	k_sleep(K_FOREVER);

	return 0;
}
