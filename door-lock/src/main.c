/* SPDX-License-Identifier: ISC */

/*
 * tinytalk door-lock sample.
 *
 * Boot order, learned from the source project's commissioning glue:
 *
 *   1. PSA Crypto, then settings: the fabric store and OpenThread keep their
 *      state there, and the node signs and derives with PSA.
 *   2. Bluetooth, then the Matter node (matter_commission_init), which restores
 *      any stored fabric and, with Thread, rejoins the network.
 *   3. Advertise the 0xFFF6 commissionable service while there is no fabric.
 *
 * From there the commissioner drives it: PASE over BLE, the fabric added over
 * CASE, then the Thread attach and the SRP registration, both done by the node
 * and ports/zephyr/thread.
 *
 * The Door Lock cluster (clusters/door_lock) sits on endpoint 1 and drives
 * LED0: on while unlocked. Button 0: a short
 * press opens a basic commissioning window for WINDOW_S seconds; a press held
 * for FACTORY_RESET_MS erases the stored fabrics and reboots. In a tinyfota
 * build the short press also opens the update window.
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#include <psa/crypto.h>

#include <tinytalk/matter_ble.h>
#include <tinytalk/matter_commission.h>
#include <tinytalk/matter_fab_store.h>
#include <tinytalk/matter_thread.h>
#include <tinytalk/tm_door_lock.h>

#if defined(CONFIG_TINYCRED)
#include "cred.h"
#endif

#if defined(CONFIG_TINYFOTA_RECEIVER)
#include <tinyfota_rx.h>
#endif

LOG_MODULE_REGISTER(door_lock, LOG_LEVEL_INF);

#define WINDOW_S         900u
#define FACTORY_RESET_MS 5000
/* The reboot after a factory reset waits at least SRP_RETRACT_MS, then every
 * SRP_RETRACT_POLL_MS while the SRP removals are in flight, up to
 * SRP_RETRACT_MAX_MS after the reset. */
#define SRP_RETRACT_MS      2000
#define SRP_RETRACT_POLL_MS 100
#define SRP_RETRACT_MAX_MS  5000

static const struct matter_commission_config s_matter_cfg = {
	.vendor_id = CONFIG_TINYTALK_VENDOR_ID,
	.product_id = CONFIG_TINYTALK_PRODUCT_ID,
	.verifier_hex = CONFIG_TINYTALK_SPAKE2P_VERIFIER,
	.salt_hex = CONFIG_TINYTALK_SPAKE2P_SALT,
	.iterations = CONFIG_TINYTALK_SPAKE2P_ITERATIONS,
	.product_name = "tinytalk door lock",
	.hardware_version = 1u,
	.software_version = 1u,
	.endpoints = &tm_door_lock_endpoint,
	.n_endpoints = 1u,
};

static const struct gpio_dt_spec s_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec s_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/* ---- advertising ------------------------------------------------------------
 *
 * The node asks for this through matter_commission_readvertise() with its own
 * lock held, so the work is done here, on the system work queue.
 */
static void adv_work_fn(struct k_work *work)
{
	bool want = !matter_commission_has_fabric() || matter_commission_window_open();

	ARG_UNUSED(work);
#if defined(CONFIG_TINYFOTA_RECEIVER)
	/* Connectable for the whole update window, which can outlast the
	 * commissioning window or open while one is already running. */
	want = want || tinyfota_window_is_open();
#endif
	/* The GATT table follows the advert: no commissioning service while the
	 * node is not offering to be commissioned. -EBUSY means a link is up and
	 * the next pass will try again. The transport repeats the last request
	 * when a connection ends, and logs every connect and disconnect. */
#if defined(CONFIG_TINYCRED)
	/* One advertiser: the commissioning advertisement while it is wanted,
	 * the credential reader's the rest of the time. Off before on. */
	if (want) {
		cred_advertise(false);
	}
#endif
	(void)matter_ble_advertise(want);
#if defined(CONFIG_TINYCRED)
	if (!want) {
		cred_advertise(true);
	}
#endif
}
static K_WORK_DEFINE(s_adv_work, adv_work_fn);

void matter_commission_readvertise(void)
{
	k_work_submit(&s_adv_work);
}

#if defined(CONFIG_TINYFOTA_RECEIVER)
static void on_update_window(bool open)
{
	ARG_UNUSED(open);
	k_work_submit(&s_adv_work);
}
#endif

/* ---- the bolt ---------------------------------------------------------------- */

void tm_door_lock_actuate(uint8_t lock_state)
{
	(void)gpio_pin_set_dt(&s_led, lock_state == MATTER_DL_LOCK_STATE_UNLOCKED);
	LOG_INF("door %s", lock_state == MATTER_DL_LOCK_STATE_UNLOCKED ? "unlocked" : "locked");
}

/* ---- the button --------------------------------------------------------------- */

static struct gpio_callback s_button_cb;
/* -1 until this boot sees a press: the press held through a factory-reset
 * reboot ends here as a bare release, which is not a short press. */
static int64_t s_pressed_at = -1;
/* When the factory reset started, for the SRP_RETRACT_MAX_MS cap. */
static int64_t s_reset_at;

static void reboot_fn(struct k_work *work)
{
#if defined(CONFIG_TINYTALK_HAS_THREAD)
	if (matter_thread_advertise_pending()) {
		if (k_uptime_get() - s_reset_at < SRP_RETRACT_MAX_MS) {
			(void)k_work_reschedule(k_work_delayable_from_work(work),
						K_MSEC(SRP_RETRACT_POLL_MS));
			return;
		}
		LOG_WRN("SRP removal still pending after %d ms; rebooting", SRP_RETRACT_MAX_MS);
	}
#else
	ARG_UNUSED(work);
#endif
	/* The fabric erase is logical: the old records, operational keys among
	 * them, stay in NVS until garbage collection, and OpenThread's dataset,
	 * network key and SRP key are not the node's to erase. Clearing the
	 * settings store takes all of it, PSA's stored keys included. */
	void *storage;

	if (settings_storage_get(&storage) == 0) {
		(void)nvs_clear(storage);
	}
	sys_reboot(SYS_REBOOT_COLD);
}
static K_WORK_DELAYABLE_DEFINE(s_reboot_work, reboot_fn);

/* The SRP records are retracted before the reboot: the border router otherwise
 * keeps advertising them for their lease (2 h asked for), and Apple Home dials
 * a stale commissionable record over Thread instead of using BLE. It connects
 * and never writes C1 (2026-10-16, dns-sd -B _matterc._udp). LED0 stays lit
 * while the removal goes out, so the reset is visible. */
static void factory_reset_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (gpio_pin_get_dt(&s_button) != 1) {
		return;
	}
	LOG_WRN("button held: factory reset");
	s_reset_at = k_uptime_get();
	(void)gpio_pin_set_dt(&s_led, 1);
	(void)matter_fab_erase();
#if defined(CONFIG_TINYTALK_HAS_THREAD)
	matter_thread_advertise_reset();
#endif
	(void)k_work_reschedule(&s_reboot_work, K_MSEC(SRP_RETRACT_MS));
}
static K_WORK_DELAYABLE_DEFINE(s_factory_reset_work, factory_reset_fn);

static void open_window_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (matter_commission_open_window(WINDOW_S) == 0) {
		LOG_INF("commissioning window open for %u s", WINDOW_S);
	} else {
		LOG_INF("commissioning window already open");
	}
#if defined(CONFIG_TINYFOTA_RECEIVER)
	tinyfota_window_open(CONFIG_TINYFOTA_WINDOW_MS);
	LOG_INF("update window open for %d ms", CONFIG_TINYFOTA_WINDOW_MS);
#endif
}
static K_WORK_DEFINE(s_open_window_work, open_window_fn);

static void on_button(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	/* Deaf while a factory reset retracts: a bounce or second press opened a
	 * window whose _matterc record the wipe orphaned, and Home dialed it until
	 * its 120 s lease ran out (2026-10-16). */
	if (k_work_delayable_is_pending(&s_reboot_work)) {
		return;
	}
	if (gpio_pin_get_dt(&s_button) == 1) {
		s_pressed_at = k_uptime_get();
		(void)k_work_reschedule(&s_factory_reset_work, K_MSEC(FACTORY_RESET_MS));
		return;
	}
	(void)k_work_cancel_delayable(&s_factory_reset_work);
	if (s_pressed_at >= 0 && k_uptime_get() - s_pressed_at < FACTORY_RESET_MS) {
		k_work_submit(&s_open_window_work);
	}
	s_pressed_at = -1;
}

static int button_init(void)
{
	int rc;

	if (!gpio_is_ready_dt(&s_button)) {
		return -ENODEV;
	}
	rc = gpio_pin_configure_dt(&s_button, GPIO_INPUT);
	if (rc == 0) {
		rc = gpio_pin_interrupt_configure_dt(&s_button, GPIO_INT_EDGE_BOTH);
	}
	if (rc == 0) {
		gpio_init_callback(&s_button_cb, on_button, BIT(s_button.pin));
		rc = gpio_add_callback(s_button.port, &s_button_cb);
	}
	return rc;
}

int main(void)
{
	int rc;

	if (gpio_is_ready_dt(&s_led)) {
		(void)gpio_pin_configure_dt(&s_led, GPIO_OUTPUT_INACTIVE);
	}
	if (button_init() != 0) {
		LOG_WRN("no button: commissioning window and factory reset unavailable");
	}

	if (psa_crypto_init() != PSA_SUCCESS) {
		LOG_ERR("PSA Crypto did not start");
		return 0;
	}
	rc = settings_subsys_init();
	if (rc != 0) {
		LOG_ERR("settings did not start (%d)", rc);
		return 0;
	}
	rc = matter_ble_start();
	if (rc != 0) {
		LOG_ERR("Bluetooth did not start (%d)", rc);
		return 0;
	}
#if defined(CONFIG_TINYCRED)
	cred_configure();
#endif
	rc = matter_commission_init(&s_matter_cfg);
	if (rc != 0) {
		LOG_ERR("Matter node did not start (%d)", rc);
		return 0;
	}

#if defined(CONFIG_TINYFOTA_RECEIVER)
	tinyfota_set_window_cb(on_update_window);
#endif
#if defined(CONFIG_TINYCRED)
	(void)cred_start();
#endif
	LOG_INF("manual pairing code %s, discriminator 0x%03x", TM_SAMPLE_MANUAL_CODE,
		CONFIG_TINYTALK_DISCRIMINATOR);
	matter_commission_readvertise();
	return 0;
}
