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

## License

Not decided yet. No license is granted until a LICENSE file is added.
