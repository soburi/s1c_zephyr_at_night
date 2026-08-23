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
#include <errno.h>
#include <inttypes.h>
#include <string.h>

#include <zenoh-pico.h>

#define SLEEP_TIME_MS	1
#define CAN_MESSAGE_ID_SELF   0x28
#define CAN_MESSAGE_ID_TARGET 0x28
#define CAN_RX_QUEUE_SIZE     16
#define LOCATOR_SIZE 96
#define KEY_SIZE 96
#define COMM_WORK_QUEUE_STACK_SIZE 1024
#define COMM_WORK_QUEUE_PRIORITY 5
#define APP_ZENOH_KEY_PREFIX "can"

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
static struct k_work zenoh_publish_work;

/**
 * デバイスツリーで led0のエイリアスが定義されていればそれを使う。オプション。
 */
static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,
						     {0});

static const struct device *const zenoh_uart_dev = DEVICE_DT_GET(DT_ALIAS(zenoh_uart));

static z_owned_session_t session;

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

static int hex_digit(char value)
{
	if (value >= '0' && value <= '9') {
		return value - '0';
	}
	if (value >= 'a' && value <= 'f') {
		return value - 'a' + 10;
	}
	if (value >= 'A' && value <= 'F') {
		return value - 'A' + 10;
	}

	return -EINVAL;
}

static int can_id_from_key(const char *key, size_t key_len, uint32_t *id)
{
	const char *prefix = APP_ZENOH_KEY_PREFIX;
	const size_t prefix_len = strlen(prefix);

	/* Expected key format: "<prefix>/<3-digit CAN ID>/tx". */
	if (key_len != prefix_len + sizeof("/000/tx") - 1 ||
	    memcmp(key, prefix, prefix_len) != 0 || key[prefix_len] != '/' ||
	    memcmp(&key[prefix_len + 4], "/tx", 3) != 0) {
		return -EINVAL;
	}

	*id = 0;
	for (size_t i = prefix_len + 1; i < prefix_len + 4; ++i) {
		const int digit = hex_digit(key[i]);

		if (digit < 0) {
			return -EINVAL;
		}
		*id = (*id << 4) | (uint32_t)digit;
	}

	return *id <= CAN_STD_ID_MASK ? 0 : -EINVAL;
}

void publish_status(uint32_t msgid, uint8_t enabled)
{
	char key[KEY_SIZE];
	z_view_keyexpr_t keyexpr;
	z_owned_bytes_t payload;
	int ret;

	ret = snprintf(key, sizeof(key), "%s/%03x/rx", APP_ZENOH_KEY_PREFIX, msgid);
	if (ret < 0 || (size_t)ret >= sizeof(key)) {
		printk("CAN -> Zenoh key is too long\n");
		return;
	}
	z_view_keyexpr_from_str_unchecked(&keyexpr, key);
	if (z_bytes_copy_from_buf(&payload, &enabled, 1) < 0) {
		printk("CAN -> Zenoh payload allocation failed\n");
		return;
	}
	if (z_put(z_loan(session), z_loan(keyexpr), z_move(payload), NULL) < 0) {
		printk("CAN -> Zenoh failed: %s\n", key);
		return;
	}
}

static void zenoh_publish_work_handler(struct k_work *work)
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

static void on_zenoh_sample(z_loaned_sample_t *sample, void *context)
{
	uint32_t id;
	z_view_string_t key;

	ARG_UNUSED(context);

	z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key);
	if (can_id_from_key(z_string_data(z_loan(key)),
			    z_string_len(z_loan(key)), &id) != 0) {
		printk("Zenoh message ignored: invalid key %.*s\n",
		       (int)z_string_len(z_loan(key)),
		       z_string_data(z_loan(key)));
		return;
	}

	if (k_msgq_put(&can_rx_queue, &id, K_NO_WAIT) != 0) {
		return;
	}

	(void)k_work_submit(&zenoh_publish_work);
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

	publish_status(CAN_MESSAGE_ID_TARGET, enabled);
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
	char subscribe_key[KEY_SIZE];
	z_view_keyexpr_t sub_key;
	z_owned_closure_sample_t callback;
	z_owned_subscriber_t subscriber;
	int ret = 0;

	if (!device_is_ready(zenoh_uart_dev)) {
		printk("Zenoh UART device is not ready\n");
		return 0;
	}

	/* CANメッセージ受信時に実行するwork の初期化. */
	k_work_init(&zenoh_publish_work, zenoh_publish_work_handler);
	char locator[LOCATOR_SIZE];
	(void)snprintf(locator, sizeof(locator), "serial/%s#baudrate=%d",
		zenoh_uart_dev->name, CONFIG_APP_ZENOH_BAUDRATE);
	(void)snprintf(subscribe_key, sizeof(subscribe_key), "%s/*/tx",
		       APP_ZENOH_KEY_PREFIX);
	printk("Connecting to zenohd at %s\n", locator);

	z_owned_config_t config;
	z_config_default(&config);
	if (zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "client") < 0 ||
	    zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, locator) < 0) {
		printk("Failed to create Zenoh configuration\n");
		z_drop(z_move(config));
		return 0;
	}

	if (z_open(&session, z_move(config), NULL) < 0) {
		printk("Could not open the Zenoh session; check UART and zenohd\n");
		return 0;
	}

	z_view_keyexpr_from_str_unchecked(&sub_key, subscribe_key);
	z_closure(&callback, on_zenoh_sample, NULL, NULL);
	if (z_declare_subscriber(z_loan(session), &subscriber, z_loan(sub_key),
				 z_move(callback), NULL) < 0) {
		printk("Could not subscribe to %s\n", subscribe_key);
		z_drop(z_move(session));
		return 0;
	}

	if (ret != 0) {
		printk("Zenoh subscriber start failed (%d)\n", ret);
		z_drop(z_move(subscriber));
		z_drop(z_move(session));
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

	printk("Ready: button PUB %s, remote SUB %s\n", CONFIG_APP_ZENOH_PUB_KEY,
		CONFIG_APP_ZENOH_SUB_KEY);

	k_sleep(K_FOREVER);

	return 0;
}
