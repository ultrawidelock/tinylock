# tinylock

The reference door lock built from tinytalk and its sibling libraries: a
Matter Door Lock on an nRF52833 (DWM3001CDK), commissioned over BLE and
operated over Thread. tinylock holds the one full-stack sample, the
swappable stack fragments, the size matrix and the Apple Home gates. It owns
no library code: every library comes in as a Zephyr module from the command
line.

## Libraries

| Module | Role | Needed by |
|---|---|---|
| tinytalk | Matter node: PASE, CASE, Interaction Model, clusters | every build |
| tinypsa | PSA Crypto provider, replaces nrf_security | `stacks/tinypsa.conf` |
| tinyble | BLE peripheral host on the SoftDevice Controller, replaces the Zephyr BT host | `stacks/tinyble.conf` |
| tinygrid | Thread 1.1 child, replaces OpenThread | `stacks/tinygrid.conf` |
| tinyfota | Signed in-place delta DFU, MCUboot applier | `stacks/tinyfota.conf` |

## Build

From inside an nRF Connect SDK v3.3.0 workspace:

```
west build -p always -b decawave_dwm3001cdk -d <tinylock>/build/<dir> <tinylock>/door-lock -- \
  -DZEPHYR_EXTRA_MODULES="<tinytalk>;<other modules>" \
  -DCONF_FILE="<fragments>"
```

`<tinytalk>` and the other module entries are paths to local checkouts.
They go on the command line only, never into a file.

## Fragments

`door-lock/prj.conf` holds the lines every build shares;
`door-lock/stacks/*.conf` holds one stack each. With no `CONF_FILE`, the
sample builds the stock set:

| Seam | Stock fragment | Library fragment |
|---|---|---|
| BLE | `stacks/bt-host.conf` | `stacks/tinyble.conf` |
| Thread | `stacks/openthread.conf` | `stacks/tinygrid.conf` |
| Crypto | `stacks/nrf-security.conf` | `stacks/tinypsa.conf`, plus `stacks/tinypsa-openthread.conf` with OpenThread |
| DFU | none | `stacks/tinyfota.conf` |

A swap replaces one stock fragment with its library fragment, for example
`-DCONF_FILE="prj.conf;stacks/bt-host.conf;stacks/openthread.conf;stacks/tinypsa.conf;stacks/tinypsa-openthread.conf"`.
`overlay-release.conf` drops logging and RTT for the release image.

## Sizes

The door-lock on decawave_dwm3001cdk, NCS v3.3.0, LTO on, from
`scripts/size_matrix.sh` on 2026-10-17: tinylock b10dfc5, tinytalk 4e14cfc,
tinyble 93eb8b3, tinygrid b8e3b4a, tinypsa d556143. The stock rows are built
with tinytalk as the only module.

| Set | Flash (B) | RAM (B) |
|---|---|---|
| stock | 304,584 | 97,440 |
| stock+overlay-release | 282,496 | 88,928 |
| tinyble+tinygrid+tinypsa | 207,144 | 69,592 |
| tinyble+tinygrid+tinypsa+overlay-release | 195,048 | 61,208 |

The last row is the release image with every library swapped in: 87,448 B
less flash and 27,720 B less RAM than the stock release. It passed an Apple
Home add, lock and unlock on the board on 2026-10-17.

## License

Not decided yet. No license is granted until a LICENSE file is added.
