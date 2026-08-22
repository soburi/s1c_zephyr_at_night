/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <zenoh-pico.h>

LOG_MODULE_REGISTER(zenoh_serial, LOG_LEVEL_INF);

#define ZENOH_UART_NODE DT_ALIAS(zenoh_uart)
#define LED_NODE DT_ALIAS(led0)
#define BUTTON_NODE DT_ALIAS(sw0)

#if !DT_NODE_EXISTS(ZENOH_UART_NODE)
#error "The board overlay must define the zenoh-uart devicetree alias"
#endif
#if !DT_NODE_HAS_STATUS_OKAY(LED_NODE)
#error "The board must provide an enabled led0 alias"
#endif
#if !DT_NODE_HAS_STATUS_OKAY(BUTTON_NODE)
#error "The board must provide an enabled sw0 alias"
#endif

#define LOCATOR_SIZE 96
#define DEBOUNCE_MS 50

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static struct gpio_callback button_callback;
static atomic_t led_state;
K_SEM_DEFINE(button_pressed, 0, 1);

static bool toggle_led(void)
{
	bool enabled = (atomic_xor(&led_state, 1) & 1) == 0;

	(void)gpio_pin_set_dt(&led, enabled);
	return enabled;
}

static void on_button(const struct device *port, struct gpio_callback *callback,
		      gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);
	k_sem_give(&button_pressed);
}

static void on_sample(z_loaned_sample_t *sample, void *context)
{
	ARG_UNUSED(context);

	bool enabled = toggle_led();
	z_view_string_t key;
	z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key);
	LOG_INF("RX %.*s: LED -> %s", (int)z_string_len(z_loan(key)),
		z_string_data(z_loan(key)), enabled ? "on" : "off");
}

static int configure_gpio(void)
{
	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&button)) {
		return -ENODEV;
	}
	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&button, GPIO_INPUT) < 0) {
		return -EIO;
	}

	atomic_clear(&led_state);
	gpio_init_callback(&button_callback, on_button, BIT(button.pin));
	if (gpio_add_callback(button.port, &button_callback) < 0 ||
	    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE) < 0) {
		return -EIO;
	}
	return 0;
}

int main(void)
{
	char locator[LOCATOR_SIZE];
	int64_t last_press = -DEBOUNCE_MS;

	if (configure_gpio() < 0) {
		LOG_ERR("Could not initialize LED or user button");
		return 0;
	}

	(void)snprintf(locator, sizeof(locator), "serial/%s#baudrate=%d",
		DEVICE_DT_NAME(ZENOH_UART_NODE), CONFIG_APP_ZENOH_BAUDRATE);
	LOG_INF("Connecting to zenohd at %s", locator);

	z_owned_config_t config;
	z_config_default(&config);
	if (zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "client") < 0 ||
	    zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, locator) < 0) {
		LOG_ERR("Failed to create Zenoh configuration");
		z_drop(z_move(config));
		return 0;
	}

	z_owned_session_t session;
	if (z_open(&session, z_move(config), NULL) < 0) {
		LOG_ERR("Could not open the Zenoh session; check UART and zenohd");
		return 0;
	}

	z_view_keyexpr_t sub_key;
	z_view_keyexpr_from_str_unchecked(&sub_key, CONFIG_APP_ZENOH_SUB_KEY);
	z_owned_closure_sample_t callback;
	z_closure(&callback, on_sample, NULL, NULL);
	z_owned_subscriber_t subscriber;
	if (z_declare_subscriber(z_loan(session), &subscriber, z_loan(sub_key),
				 z_move(callback), NULL) < 0) {
		LOG_ERR("Could not subscribe to %s", CONFIG_APP_ZENOH_SUB_KEY);
		z_drop(z_move(session));
		return 0;
	}

	z_view_keyexpr_t pub_key;
	z_view_keyexpr_from_str_unchecked(&pub_key, CONFIG_APP_ZENOH_PUB_KEY);
	z_owned_publisher_t publisher;
	if (z_declare_publisher(z_loan(session), &publisher, z_loan(pub_key), NULL) < 0) {
		LOG_ERR("Could not declare publisher for %s", CONFIG_APP_ZENOH_PUB_KEY);
		z_drop(z_move(subscriber));
		z_drop(z_move(session));
		return 0;
	}

	LOG_INF("Ready: button PUB %s, remote SUB %s", CONFIG_APP_ZENOH_PUB_KEY,
		CONFIG_APP_ZENOH_SUB_KEY);
	while (true) {
		k_sem_take(&button_pressed, K_FOREVER);
		int64_t now = k_uptime_get();
		if (now - last_press < DEBOUNCE_MS) {
			continue;
		}
		last_press = now;

		bool enabled = toggle_led();
		const char *state = enabled ? "on" : "off";
		z_owned_bytes_t payload;
		z_bytes_copy_from_str(&payload, state);
		if (z_publisher_put(z_loan(publisher), z_move(payload), NULL) < 0) {
			LOG_WRN("Button publish failed");
		} else {
			LOG_INF("Button: LED -> %s, TX %s = %s", state,
				CONFIG_APP_ZENOH_PUB_KEY, state);
		}
	}

	return 0;
}
