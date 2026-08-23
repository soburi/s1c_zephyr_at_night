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
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>

#define SLEEP_TIME_MS	1

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

/**
 * デバイスツリーで led0のエイリアスが定義されていればそれを使う。オプション。
 */
static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,
						     {0});

/**
 * ボタン押下時の処理
 */
void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
}

/*
 * main
 */
int main(void)
{
	int ret;

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
	if (led.port) {
		while (1) {
			/* ボタンの状態を取得 */
			int val = gpio_pin_get_dt(&button);

			/* エラーでなければ、LEDにボタンの状態と同じ値を設定 */
			if (val >= 0) {
				gpio_pin_set_dt(&led, val);
			}
			
			/* 1ms周期で更新. ほぼ即座に状態は反映される */
			k_msleep(SLEEP_TIME_MS);
		}
	}
	return 0;
}
