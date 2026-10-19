/* SPDX-License-Identifier: Apache-2.0 */

/*
 * The credential reader on this lock. tinycred runs the phone's transaction
 * on tinyble's channel and brings ranging up on tinyuwb; this file turns the
 * ranges tinyuwb latches into bolt moves through tinycred's approach
 * controller, and tells the phone and the Matter tile.
 *
 * One thread, woken by every latched range and every TICK_MS: the approach
 * controller is fed trusted ranges, sees untrusted ones for departure only,
 * and ticks in silence. A session ending is a departure too, except for the
 * mid-phase flap that reconnects within seconds, which holds the bolt for a
 * while first.
 */
#include "cred.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <tinycred/approach.h>
#include <tinycred/ble_tinyble.h>
#include <tinycred/reader.h>
#include <tinytalk/tm_door_lock.h>
#include <tinyuwb/range.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(cred, LOG_LEVEL_INF);

#define TICK_MS            250
#define FLAP_HOLD_MS       10000
#define FLAP_FEED_FRESH_MS 3000
/* OperationSourceEnum Aliro (door-lock-cluster.xml, OperationSourceEnum). */
#define OP_SOURCE_ALIRO    10u

static K_SEM_DEFINE(s_range_sig, 0, 1);
static struct tc_approach s_approach;
static bool s_granted, s_present;

static void on_range(void)
{
	k_sem_give(&s_range_sig);
}

/* The bolt, the phone (Reader Status Unsecured/Secured, the Wallet
 * animation), then the tile. Once per transition. */
static void bolt(bool unlocked)
{
	uint8_t state = unlocked ? MATTER_DL_LOCK_STATE_UNLOCKED : MATTER_DL_LOCK_STATE_LOCKED;

	if (s_granted == unlocked) {
		return;
	}
	s_granted = unlocked;
	LOG_INF("approach: %s", unlocked ? "unlock" : "relock");
	tm_door_lock_actuate(state);
	tc_reader_notify_unlock(unlocked);
	tm_door_lock_set(state, OP_SOURCE_ALIRO);
}

static void loop(void *a, void *b, void *c)
{
	uint32_t last_gen = tu_rs_generation(), last_obs = last_gen;
	int64_t flap_hold = 0;
	bool session_was = false;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (;;) {
		int64_t now = k_uptime_get();
		uint32_t gen = tu_rs_generation();
		bool session = tc_reader_session_active();
		enum tc_approach_action act;
		int32_t cm = 0;

		tc_reader_status_tick(now);
		if (session && !session_was) {
			/* The RSSI gate holds ranging off until the phone is at
			 * the door, so the session coming up is the approach
			 * evidence the controller's trajectory gate wants. */
			tc_approach_session_up(&s_approach);
			s_present = true;
		}
		session_was = session;

		if (gen != last_gen && tu_rs_trusted() &&
		    tu_rs_last_range(&cm, NULL, NULL, NULL, NULL)) {
			last_gen = last_obs = gen;
			act = tc_approach_feed(&s_approach, now, cm);
		} else {
			if (gen != last_obs && tu_rs_last_range(&cm, NULL, NULL, NULL, NULL)) {
				last_obs = gen;
				tc_approach_observe_departure(&s_approach, now, cm);
			}
			act = tc_approach_tick(&s_approach, now);
		}
		switch (act) {
		case TC_APPROACH_UNLOCK_PREDICT:
		case TC_APPROACH_UNLOCK_THRESHOLD:
			bolt(true);
			break;
		case TC_APPROACH_RELOCK_DEPART:
		case TC_APPROACH_RELOCK_ABORT:
			bolt(false);
			break;
		default:
			break;
		}

		if (session) {
			flap_hold = 0;
		} else if (s_present) {
			if (flap_hold == 0 && s_granted && s_approach.last_feed_ms != 0 &&
			    !tc_reader_last_close_established() &&
			    s_approach.last_cm < s_approach.cfg.relock_cm &&
			    now - s_approach.last_feed_ms <= FLAP_FEED_FRESH_MS) {
				flap_hold = now;
				LOG_WRN("session died with the phone at %d cm; holding the bolt",
					s_approach.last_cm);
			}
			if (flap_hold == 0 || now - flap_hold > FLAP_HOLD_MS) {
				flap_hold = 0;
				(void)tc_approach_gone(&s_approach);
				bolt(false);
				s_present = false;
			}
		}
		(void)k_sem_take(&s_range_sig, K_MSEC(TICK_MS));
	}
}

K_THREAD_DEFINE(cred_thread, 2048, loop, NULL, NULL, NULL, K_PRIO_PREEMPT(10), 0,
		K_TICKS_FOREVER);

/* ---- Matter provisioning: the Door Lock cluster's Aliro hooks ---------------
 *
 * The reader identity is group_id || group_sub_id; the issuer keys (type 6)
 * have their own store in tinycred, the endpoint keys (7, 8) are trust
 * anchors. The reader persists each one before returning, which is what the
 * cluster's contract asks.
 */
static int on_reader_config(const uint8_t signing_key[32], const uint8_t verification_key[65],
			    const uint8_t group_id[16], const uint8_t group_sub_id[16],
			    const uint8_t grk[16])
{
	uint8_t reader_id[32];
	int rc;

	ARG_UNUSED(verification_key);
	memcpy(reader_id, group_id, 16u);
	memcpy(reader_id + 16, group_sub_id, 16u);
	rc = tc_reader_provision_identity(reader_id, signing_key, grk);
	memset(reader_id, 0, sizeof(reader_id));
	return rc;
}

static int on_reader_clear(void)
{
	return tc_reader_provision_clear();
}

static int on_credential_set(uint8_t cred_type, const uint8_t key[65], uint16_t cred_index,
			     uint16_t user_index)
{
	int rc = cred_type == MATTER_DL_CRED_ALIRO_ISSUER_KEY
			 ? tc_reader_provision_add_issuer(key, cred_index, user_index)
			 : tc_reader_provision_add_trust(key, cred_type, cred_index, user_index);

	return rc; /* 0 added, 1 already there, < 0 refused */
}

static int on_credential_clear(uint8_t cred_type, uint16_t cred_index)
{
	int rc;

	if (cred_index == MATTER_DL_INDEX_ALL) {
		rc = tc_reader_provision_remove_type(cred_type);
	} else if (cred_type == MATTER_DL_CRED_ALIRO_ISSUER_KEY) {
		rc = tc_reader_provision_remove_issuer(cred_index);
	} else {
		rc = tc_reader_provision_remove_trust(cred_type, cred_index);
	}
	return rc < 0 ? rc : 0;
}

static int on_user_clear(uint16_t user_index)
{
	int rc = tc_reader_provision_remove_user(user_index);

	return rc < 0 ? rc : 0;
}

static const struct tm_door_lock_aliro_hooks k_aliro_hooks = {
	.reader_config = on_reader_config,
	.reader_clear = on_reader_clear,
	.credential_set = on_credential_set,
	.credential_clear = on_credential_clear,
	.credential_lookup = tc_reader_provision_cred_lookup,
	.user_clear = on_user_clear,
};

void cred_configure(void)
{
	tm_door_lock_aliro_configure(&k_aliro_hooks);
}

int cred_start(void)
{
	uint8_t reader_id[32], verif[65], grk[16];
	int rc = tc_reader_start_attached();

	if (rc != 0) {
		LOG_ERR("credential reader did not start (%d)", rc);
		return rc;
	}
	/* What the reader loaded, for the attributes a controller reads back. */
	if (tc_reader_identity_public(reader_id, verif, grk) == 0) {
		tm_door_lock_aliro_set_identity(verif, reader_id, grk);
	}
	tc_approach_init(&s_approach, NULL);
	tu_rs_set_range_listener(on_range);
	k_thread_start(cred_thread);
	return 0;
}

void cred_advertise(bool on)
{
	tc_ble_tinyble_advertise(on);
}
