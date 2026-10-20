# tinylock

A Matter door lock on an nRF52833 (DWM3001CDK): commissioned over BLE, run
over Thread, unlocked by a phone over UWB. It owns no library code.

| Module      | Role          |    Flash |     RAM |
| :---------- | :------------ | -------: | ------: |
| tinytalk    | Matter node   |  52.6 KB | 20.9 KB |
| tinygrid    | Thread child  |  19.3 KB |  9.9 KB |
| tinypsa     | PSA Crypto    |  17.8 KB |  1.5 KB |
| tinycred    | Aliro reader  |  12.7 KB |  6.6 KB |
| tinyuwb     | UWB ranging   |   8.5 KB |  0.9 KB |
| tinyble     | BLE host      |   6.7 KB |  2.5 KB |
| tinyfota    | Delta DFU     |   4.7 KB |  1.0 KB |
| Whole image |               | 197.9 KB | 73.3 KB |

LTO release build.

## Build

```sh
eval "$(./bootstrap)"   # fetches the modules, sets TINYLOCK_MODULES

west build -b decawave_dwm3001cdk <tinylock>/door-lock -- \
  -DZEPHYR_EXTRA_MODULES="$TINYLOCK_MODULES" -DCONF_FILE="<fragments>"
```

One fragment per module in `door-lock/stacks/`. Leave `CONF_FILE` out for
the stock stack. Add `overlay-release.conf` for release.

<details>
<summary>The whole image, as measured</summary>

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

Apache-2.0
