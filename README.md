# tinylock

A Matter door lock on an nRF52833 (DWM3001CDK): commissioned over BLE, run
over Thread, unlocked by a phone over UWB.

| Module          | Role         |        Flash |       RAM |
| :-------------- | :----------- | -----------: | --------: |
| tinytalk        | Matter node  |      52.6 KB |   20.9 KB |
| tinygrid        | Thread child |      19.3 KB |    9.9 KB |
| tinypsa         | PSA Crypto   |      17.8 KB |    1.5 KB |
| tinycred        | Aliro reader |      12.7 KB |    6.6 KB |
| tinyuwb         | UWB ranging  |       8.5 KB |    0.9 KB |
| tinyble         | BLE host     |       6.7 KB |    2.5 KB |
| tinyfota        | Delta DFU    |       4.7 KB |    1.0 KB |
| **Whole image** |              | **197.9 KB** | **73.3 KB** |

LTO release build.

## Get started

Flash the [latest release](../../releases/latest), then add the lock in Apple
Home with setup code `3650-503-5696`. Home flags it as uncertified: it carries
Matter's test attestation.

```sh
nrfutil install device
nrfutil device program --firmware tinylock-*-factory.hex --options chip_erase_mode=ERASE_ALL
```

Button 0 reopens pairing for 15 minutes; held for 5 s, it resets the lock.
LED0 is lit while unlocked.

## Build

From an NCS v3.3.0 workspace:

```sh
eval "$(<tinylock>/bootstrap)"
west build -b decawave_dwm3001cdk <tinylock>/door-lock -- \
  -DZEPHYR_EXTRA_MODULES="$TINYLOCK_MODULES"
```

That builds the stock stacks. Each module has a fragment in `door-lock/stacks/`;
the release image uses them all:

<details>
<summary>The whole image</summary>

```sh
west build -b decawave_dwm3001cdk <tinylock>/door-lock -- \
  -DZEPHYR_EXTRA_MODULES="$TINYLOCK_MODULES" \
  -DCONF_FILE="prj.conf;stacks/tinyble.conf;stacks/tinygrid.conf;stacks/tinygrid-radio.conf;stacks/tinypsa.conf;stacks/tinyfota-tinyble.conf;stacks/tinyuwb.conf;stacks/tinycred.conf;overlay-release.conf" \
  -DEXTRA_DTC_OVERLAY_FILE="stacks/tinyuwb.overlay;stacks/tinyble.overlay" \
  -DSB_CONF_FILE=stacks/tinyfota/sysbuild.conf \
  -DPM_STATIC_YML_FILE=stacks/tinyfota/pm_static.yml \
  -Dmcuboot_CONF_FILE=<tinylock>/door-lock/stacks/tinyfota/mcuboot.conf
```

</details>

A release pins every library in `modules.lock`: `./bootstrap --locked` checks
those commits out, and `scripts/release.sh` builds one.

Apache-2.0

Unofficial. Not affiliated with, endorsed or certified by any company or
standards body named here. Trademarks belong to their owners.
