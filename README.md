# tinylock

A Matter door lock on an nRF52833 (DWM3001CDK): commissioned over BLE, run
over Thread, unlocked by a phone over UWB. It owns no library code.

| Module      | Role          |     Flash |      RAM |
| :---------- | :------------ | --------: | -------: |
| tinytalk    | Matter node   |  53,836 B | 21,432 B |
| tinygrid    | Thread child  |  19,721 B | 10,148 B |
| tinypsa     | PSA Crypto    |  18,216 B |  1,501 B |
| tinycred    | Aliro reader  |  12,955 B |  6,807 B |
| tinyuwb     | UWB ranging   |   8,680 B |    958 B |
| tinyble     | BLE host      |   6,895 B |  2,549 B |
| tinyfota    | Delta DFU     |   4,804 B |  1,064 B |
| Whole image |               | 202,648 B | 75,092 B |

LTO release build. Stock Zephyr, OpenThread and nrf_security, without UWB,
credentials or DFU, take 282,496 B flash and 88,928 B RAM.

## Build

```sh
eval "$(./bootstrap)"   # fetches the modules, sets TINYLOCK_MODULES

west build -b decawave_dwm3001cdk <tinylock>/door-lock -- \
  -DZEPHYR_EXTRA_MODULES="$TINYLOCK_MODULES" -DCONF_FILE="<fragments>"
```

One fragment per module in `door-lock/stacks/`. Leave `CONF_FILE` out for
the stock stack. Add `overlay-release.conf` for release.

Apache-2.0
