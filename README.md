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
| tinyfota | Signed in-place delta DFU, MCUboot applier | `stacks/tinyfota.conf`, or `stacks/tinyfota-tinyble.conf` with tinyble |

## Build

`eval "$(./bootstrap)"` clones or updates every library next to this
checkout, installs nrfutil and NCS v3.3.0 when they are missing, and sets
`TINYLOCK_MODULES`. Then, from inside an nRF Connect SDK v3.3.0 workspace:

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
| Thread | `stacks/openthread.conf` | `stacks/tinygrid.conf`, plus `stacks/tinygrid-radio.conf` for tinygrid's own radio driver in place of Nordic's |
| Crypto | `stacks/nrf-security.conf` | `stacks/tinypsa.conf`, plus `stacks/tinypsa-openthread.conf` with OpenThread |
| DFU | none | `stacks/tinyfota.conf` on the Zephyr host, `stacks/tinyfota-tinyble.conf` on tinyble |

A swap replaces one stock fragment with its library fragment, for example
`-DCONF_FILE="prj.conf;stacks/bt-host.conf;stacks/openthread.conf;stacks/tinypsa.conf;stacks/tinypsa-openthread.conf"`.
`overlay-release.conf` drops logging and RTT for the release image.

## Sizes

The door-lock on decawave_dwm3001cdk, NCS v3.3.0, LTO on, from
`scripts/size_matrix.sh` on 2026-10-17: tinylock 2fb2637, tinytalk 4e14cfc,
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

`stacks/tinygrid-radio.conf` after `stacks/tinygrid.conf` has tinygrid drive
the radio itself, in MPSL timeslots, instead of linking Nordic's 802.15.4
driver. The release set with and without it on 2026-10-18 (tinylock 208d96b,
tinytalk 4e14cfc, tinyble 8ecda5d, tinygrid b8e3b4a, tinypsa d556143):

| Set | Flash (B) | RAM (B) |
|---|---|---|
| tinyble+tinygrid+tinypsa+overlay-release | 195,096 | 61,208 |
| tinyble+tinygrid+tinygrid-radio+tinypsa+overlay-release | 172,448 | 58,968 |

22,648 B less flash and 2,240 B less RAM. It passed an Apple Home add, lock
and unlock on the board on 2026-10-18, the first run of tinygrid's driver
on hardware.

The same libraries set plus `stacks/tinyfota-tinyble.conf`, built with
sysbuild and MCUboot from `stacks/tinyfota/` on 2026-10-17 (tinylock
6e22bd8, tinyble 8ecda5d, tinyfota 7ec5d9c, the other modules as above):

| Image | Flash (B) | RAM (B) |
|---|---|---|
| door-lock | 215,824 | 70,680 |
| MCUboot | 25,512 | 14,464 |

On the board on 2026-10-17 it passed an Apple Home add, then a delta
update over SMP (`tinyfota push`, 4,186 B staged in 4.4 s) and one over
tinyfota's native frames (`tinyfota push --native`, 4,177 B), with the
fabrics kept and Home controlling the lock after each.

### Where the release image goes

The last row above, split by source. Each sized symbol in `zephyr.elf` is
assigned to the file that defines it, from the DWARF debug info: code by the
outermost frame `addr2line -i` gives for its address, data by the
`DW_AT_decl_file` of its variable. Zephyr's `rom_report` cannot do this with
LTO on: it puts 98% of the image under "(no paths)". LTO also inlines across
modules, so code inlined from one library counts toward its caller. RAM
includes initialised data, which also takes the same bytes in flash.

| Component | Flash (B) | % of flash | RAM (B) | % of RAM |
|---|---|---|---|---|
| tinytalk (Matter) | 50,228 | 25.8% | 19,811 | 32.4% |
| tinygrid (Thread) | 16,926 | 8.7% | 9,309 | 15.2% |
| tinypsa (crypto) | 15,346 | 7.9% | 1,501 | 2.5% |
| tinyble (BLE host) | 4,861 | 2.5% | 2,099 | 3.4% |
| **tiny\* total** | **87,361** | **44.8%** | **32,720** | **53.5%** |
| SoftDevice Controller and MPSL (Nordic binary libraries) | 51,426 | 26.4% | 3,202 | 5.2% |
| Zephyr (kernel, drivers, subsystems, generated code) | 23,540 | 12.1% | 19,319 | 31.6% |
| nRF 802.15.4 radio driver | 22,797 | 11.7% | 2,465 | 4.0% |
| NCS and hal_nordic glue | 3,376 | 1.7% | 174 | 0.3% |
| C library and compiler runtime | 2,026 | 1.0% | 0 | 0% |
| door-lock app | 954 | 0.5% | 156 | 0.3% |
| Unattributed and alignment padding | 3,568 | 1.8% | 3,172 | 5.2% |

## License

Not decided yet. No license is granted until a LICENSE file is added.
