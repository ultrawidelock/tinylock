/* SPDX-License-Identifier: Apache-2.0 */

/*
 * The credential reader on this lock (CONFIG_TINYCRED): tinycred on tinyble,
 * ranging on tinyuwb, the approach controller, the bolt. See cred.c.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Before the Matter node: the Door Lock cluster's Aliro hooks. */
void cred_configure(void);

/* After the Matter node is up: start the reader and the approach loop. */
int cred_start(void);

/* The bolt moved on a controller's command: a connected phone's Wallet hears
 * it, so it does not keep showing the state its own last grant left. */
void cred_bolt_moved(uint8_t lock_state);

/* The reader's advertisement on tinyble's one advertiser. The commissioning
 * advertisement takes the advertiser while a window is open; the reader has it
 * the rest of the time. */
void cred_advertise(bool on);
