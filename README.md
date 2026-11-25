# Full Parameter List (`fpl`)

**English** · [Українська](README_UA.md)

A terminal (TUI) parameter editor for **ArduPilot** and **PX4** over MAVLink. The command is `fpl`.
The table has the same columns as Mission Planner's "Full Parameter List":
`Command | Value | Units | Options | Desc | Fav`.

- Plain C (C11), UI built with [notcurses](https://github.com/dankamongmen/notcurses),
  protocol via [mavlink/c_library_v2](https://github.com/mavlink/c_library_v2) (`ardupilotmega` dialect).
- Finds the vehicle automatically like QGroundControl does, or connects to what you give on the command line.
- Downloads parameters re-requesting missing indices; writes with retries and checks the vehicle's answer.
- Parameter metadata (units, ranges, enums, bitmasks, descriptions) is downloaded automatically and cached.

## Building

Requirements: CMake ≥ 3.20, a C compiler, `pkg-config`, `libcurl`, `expat`, `ncursesw`,
`libunistring`, `zlib` (on Arch: `pacman -S curl expat ncurses libunistring zlib`).

```sh
cmake -S . -B build
cmake --build build -j
./build/fpl
```

CMake downloads MAVLink (a pinned revision). If the system has `notcurses-core` ≥ 3.0
it is used; otherwise notcurses 3.0.17 is built statically inside `build/` (no multimedia, no C++).
cJSON likewise: the system `libcjson` ≥ 1.7.19, otherwise the bundled 1.7.19.
To force the bundled ones: `-DFPL_SYSTEM_NOTCURSES=OFF`, `-DFPL_SYSTEM_CJSON=OFF`.
Sanitizer build: `-DFPL_ASAN=ON`.

### Packages (deb, rpm)

`tools/pkg/build.sh` builds the packages in a container, so it works the same on any distribution.
They go to `dist/<arch>/`:

| | deb | rpm |
|---|---|---|
| Built on | Debian 13 | AlmaLinux 10 |
| Files | `fpl_0.1.0_amd64.deb`, `fpl-dbgsym_…ddeb` | `fpl-0.1.0-1.x86_64.rpm`, `fpl-debuginfo-…rpm` |
| Installs on | Debian 13, Ubuntu 24.04 or newer | Fedora, openSUSE Leap 16 / Tumbleweed, RHEL / AlmaLinux / Rocky 10 |

```sh
tools/pkg/build.sh deb             # for the architecture of this machine
tools/pkg/build.sh rpm arm64       # amd64 or arm64; another architecture runs under QEMU
tools/pkg/install-test.sh rpm      # install and start it in clean containers of those systems

sudo apt install ./dist/amd64/fpl_*.deb
sudo dnf install ./dist/amd64/fpl-0*.rpm      # zypper install --allow-unsigned-rpm on openSUSE
```

- **What you need:** Podman, Docker or Apple `container` (the first one found; `CONTAINER_ENGINE=` picks
  one). For another architecture you also need QEMU user emulation
  (`pacman -S qemu-user-static qemu-user-static-binfmt`, `apt install qemu-user-binfmt`).
  The build downloads MAVLink, cJSON and notcurses, so it needs the network.
- **What's inside:** notcurses is linked statically (Debian 13 has none). The other libraries (curl, expat,
  ncurses, libunistring, zlib) become dependencies: through `dpkg-shlibdeps` in the deb, as sonames
  (`libcurl.so.4()(64bit)`, …) in the rpm. Sonames don't depend on package names, so one rpm fits every
  RPM distribution. AlmaLinux 10 has the oldest glibc among them (2.39), so the rpm needs no newer one.
- **Portability of the bundled notcurses:** it is built with zlib instead of libdeflate (RHEL 10 has
  libdeflate only in EPEL), and its `del_curterm(cur_term)` becomes `del_curterm(set_curterm(NULL))`:
  openSUSE builds ncurses with the reentrant ABI, where `cur_term` is a function, and a binary built
  elsewhere would stop there with "undefined symbol: cur_term" (`cmake/Notcurses.cmake`).
- **Version:** the one in `project()` of `CMakeLists.txt`. To install a new build over an old one, raise it.
  The rpm release (`-1`) counts the packagings of one version.
- **Maintainer** (deb) / **Vendor** (rpm): `$DEB_MAINTAINER` if set (CI uses the author's GitHub noreply
  address), otherwise `git config user.name` / `user.email` of whoever builds the package.
- Settings: `cmake/DebPackage.cmake`, `cmake/RpmPackage.cmake`; containers and scripts: `tools/pkg/`.

### Package for Arch Linux

`tools/pkg/arch/PKGBUILD` builds from the release tarball of GitHub, with notcurses and cJSON from
`[extra]` and MAVLink as a source of the PKGBUILD; `check()` runs `tools/ci/smoke.sh`. On Arch,
`tools/pkg/arch/build.sh` builds it from the working tree (no container) into `dist/arch/` and checks
it with `namcap`:

```sh
sudo pacman -S --needed base-devel cmake notcurses cjson curl expat namcap
tools/pkg/arch/build.sh
sudo pacman -U dist/arch/fpl-0.1.0-1-x86_64.pkg.tar.zst
```

The checksum of the release tarball is `SKIP` until the release the package is published for
(then `updpkgsums`); `pkgver` has to equal `project()` of `CMakeLists.txt`.

### CI and releases

GitHub Actions (`.github/workflows/ci.yml`) runs on every push and pull request:

- `clang-format` 23.1.3 check;
- an ASan build with `-Werror` on Ubuntu 24.04 and the smoke test `tools/ci/smoke.sh`;
- deb and rpm for amd64 and arm64 (native ARM runner, no QEMU) via `tools/pkg/build.sh`, then
  `tools/pkg/install-test.sh`: they install and start on Debian 13, Ubuntu 24.04 and 26.04 (deb) and on
  Fedora, AlmaLinux 10, openSUSE Leap 16.0 and Tumbleweed (rpm). The packages are kept as artifacts of
  the run.

A release is a tag. Raise `project(fpl VERSION …)` in `CMakeLists.txt`, commit, then:

```sh
git tag v0.2.0 && git push origin v0.2.0
```

When all the checks pass, the workflow publishes a GitHub release with the deb and rpm packages for both
architectures and notes generated from the commits. The tag has to match the version in `CMakeLists.txt`,
otherwise there is no release.

## Running

```sh
fpl                          # auto-detect
fpl /dev/ttyACM0             # USB (the baud rate doesn't matter for CDC)
fpl /dev/ttyUSB0:57600       # telemetry radio
fpl udp:14550                # listen on UDP (PX4 SITL, companion computer, ESP telemetry)
fpl udpout:192.168.4.1:14550 # send to a given address
fpl tcp:127.0.0.1:5760       # ArduPilot SITL
fpl --list                   # show the serial ports found and how they are classified
fpl --dump > params.tsv      # no UI: download the parameters, print TSV and exit
```

More options: `fpl --help`.

### Auto-detection

Like QGroundControl's `LinkManager`:

- **USB**: `ttyACM*`/`ttyUSB*` are enumerated through sysfs and classified by USB VID/PID
  (3DR/PX4, ArduPilot ChibiOS `1209:5740/5741`, CubePilot, Holybro, CUAV, ARK, NXP, STM32 VCP,
  SiK radios on FTDI/CP210x). Unknown `ttyACM*` ports are tried too, unknown USB-UARTs (`ttyUSB*`) are not.
  Ports in bootloader mode are skipped, and a port is given 1 s after it appears.
  For radios, the baud rate alternates between 57600 and 115200 until something is recognised.
- **UDP**: listens on `0.0.0.0:14550`.
- **TCP**: tries `127.0.0.1:5760` (ArduPilot SITL; QGC doesn't do this, Mission Planner does).

A GCS HEARTBEAT is sent at 1 Hz on every candidate (PX4 only starts MAVLink on USB after it sees
traffic). The first link that delivers an autopilot HEARTBEAT wins.
If the link drops it is reopened every second; the table and unwritten edits are kept.

If `/dev/ttyACM*` can't be opened, the search screen tells you which group to join
(`uucp` on Arch).

## Parameter protocol

- **ArduPilot:** parameters are downloaded the way QGroundControl and Mission Planner do it: as one file,
  `@PARAM/param.pck?withdefaults=1`, over MAVFTP. That takes under 1 s over USB instead of ~18 s, and the
  firmware's default values (which account for the board and frame) come with the values. Packets lost in the
  middle of the stream are read again separately. The size ArduPilot reports when the file is opened is only an
  estimate, so the end of the file is where EOF arrives.
  If the file doesn't exist, the vehicle doesn't answer or MAVFTP is too slow, the regular
  `PARAM_REQUEST_LIST` below is used. `--no-ftp` disables MAVFTP.

- `PARAM_REQUEST_LIST` (retried up to 4 times if the vehicle is silent), then missing indices are
  re-requested with `PARAM_REQUEST_READ` in batches of 32. The next batch goes out as soon as the previous one
  has arrived, or after 0.6 s of silence on slow links.
- Value encoding: C-cast for ArduPilot, byte-wise for PX4. Taken from `AUTOPILOT_VERSION.capabilities`
  (`MAV_CMD_REQUEST_MESSAGE`, falling back to `REQUEST_AUTOPILOT_CAPABILITIES`), and from the autopilot type
  until that answer arrives.
- Writing: `PARAM_SET`, up to 8 at a time, confirmed by a `PARAM_VALUE` echo with the same value,
  4 attempts with a 1.2 s timeout. If the vehicle returns another value, it is shown as an error.
- `STATUSTEXT` messages from the flight controller appear in the status line.

## Rebooting the flight controller

`R` reboots the flight controller (`MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN`), for example after writing a parameter that
only takes effect after a reboot: the write message then ends with "(R)".

- It asks first and needs `y`: `Enter` does not confirm it.
- Refused while the vehicle is armed (the autopilot refuses it too) and while a write is in progress.
- The command is sent once and never repeated by itself: a repeat that arrives after a reboot whose answer got lost
  would restart the vehicle a second time.
- fpl then waits for the vehicle to go away (the USB port disappears, or the heartbeat stops) and come back,
  without the usual "link lost" errors. The title shows `REBOOTING`.
- It downloads the parameters again once the vehicle has finished starting up: ArduPilot registers parameters
  while it starts (a SpeedyBee F4 had 928 of its 1082 two seconds after the first heartbeat) and reports
  `MAV_STATE_BOOT` in the heartbeat until then. Parameters of hardware found later, such as `GPS1_*` once a GPS is
  detected, can appear after that; `r` loads them.
- Edits not written yet stay in the table.

## Default values

The default comes from the firmware (ArduPilot, over MAVFTP), or else from the metadata (PX4, the `default`
field). As in QGC, the firmware's value wins. PX4's metadata defaults are generic and don't account for the
airframe, so the `n` filter also shows what the airframe set. Parameters with an unknown default
(for example ArduPilot with `--no-ftp`) don't appear in the `n` filter.

## Parameter groups

As in QGroundControl, parameters are organised in two levels: category → group.

- **PX4:** category and group come from the metadata (`category`, `group`, e.g. "Multicopter Position Control").
  Group names are translated when PX4 has a translation.
- **ArduPilot:** the category is the `User` field (Standard / Advanced). The group is the name up to the first `_`
  without trailing digits, so `SERIAL1_BAUD` and `SERIAL2_PROTOCOL` go to the "SERIAL" group. Groups with a single
  parameter are moved to "Misc".
- The "Standard" category comes first and "Other" last. The "Misc" group is last within its category.

## Metadata

| Firmware | Source | Cache |
|---|---|---|
| ArduPilot | `autotest.ardupilot.org/Parameters/versioned/<Copter\|Plane\|Rover\|Sub\|Tracker>/stable-X.Y.Z/apm.pdef.xml` for official releases, otherwise `…/Parameters/<ArduCopter\|…>/apm.pdef.xml` (master) | forever for versioned files, 7 days for master |
| PX4 | `PX4ParameterFactMetaData.json` from the QGroundControl repository | 7 days |

The cache lives in `$XDG_CACHE_HOME/fpl` (`~/.cache/fpl`). `--offline` downloads nothing,
`--meta FILE` uses your own `apm.pdef.xml` or PX4 `.json`.

### Translated parameter descriptions

- **PX4**: the official community translations from
  [PX4/PX4-Metadata-Translations](https://github.com/PX4/PX4-Metadata-Translations) (Crowdin) are used,
  file `translated/parameters_<ll_CC>.ts`, cached for 7 days. The language comes from `LANGUAGE` / `LC_MESSAGES`;
  `--meta-lang uk_UA` sets it explicitly (`--meta-lang en` disables translation). About 20% of the strings are
  translated into Ukrainian by the community (short descriptions, enum values, bits).
- A translation is applied only if its English `<source>` matches the current description.
  A translation of an older version of the text is never shown.
- The `t` key switches the whole table between the translation and the English original; the title shows
  `uk_UA` or `EN`.
- Search looks in both the translation and the original. `--dump` always prints English.
- **PX4 machine translation.** Strings missing from the community translation are filled from the machine
  translation in `translations/px4/parameters_uk_UA.mt.ts`. It was made with Claude Sonnet following the PX4
  translation guide and term sheet and checked mechanically, but nobody has reviewed it. Community translations
  always win. Such descriptions are marked "machine translation" in the details pane.
  How it was generated: `tools/px4-translate/`.
- **ArduPilot** has no official translations, so every description is machine translated:
  `translations/ardupilot/parameters_uk_UA.mt.ts` holds 10,244 unique strings from the master metadata of all
  vehicle types (Copter, Plane, Rover, Sub, AntennaTracker, Blimp, Heli) and the latest stable versions. It is keyed
  by the English text, so it fits any vehicle and version with the same description, and a description changed in
  ArduPilot is shown in English. Flight mode names stay in English. How it was generated:
  `tools/ardupilot-translate/`.
- PX4 translations can be completed in PX4's Crowdin, which also benefits QGroundControl users.

## Keys

| Key | Action |
|---|---|
| `↑↓` `PgUp PgDn` `Home End`, mouse wheel | navigation |
| `Enter` / `e` / double click | edit the value: a list for enums, checkboxes for bitmasks, text otherwise (`↑↓` step by `Increment`) |
| `w` | write the edited parameters to the vehicle (like "Write Params" in MP) |
| `r` | re-download all parameters |
| `R` | reboot the flight controller, then re-download the parameters (asks first, `y` to confirm) |
| `z` / `Z` | undo the edit of this / all parameters |
| `/` | search names and descriptions (name matches first), `Esc` clears |
| `m` | only parameters edited in this session (not written yet, or failed to write) |
| `n` | only parameters that differ from the default, like "Modified" in QGroundControl. Such values are highlighted and the default is shown in the details pane |
| `g` | parameter groups like QGroundControl (category → group). Typing filters the group list right away (`Esc` clears the filter, or closes the dialog when it is empty), `Enter` selects; the selected group filters the table, and `Esc` in the table removes that filter |
| `f` / `Space` / click in the Fav column | favourite (favourites sort first, stored in `~/.config/fpl/favorites`) |
| `v` | multi-line rows, like Mission Planner |
| `d` | details pane (type, range, step, default, reboot required, full description) |
| `t` | parameter descriptions: translation / English original |
| `s` / `l` | save / load a parameter file |
| `?` | help |
| `q` / `Ctrl-C` | quit (asks first if there are unwritten edits) |

Values outside the recommended range need a second `Enter`.
Read-only parameters can't be edited.

Files are saved in Mission Planner format (`NAME,VALUE`). Mission Planner and QGroundControl
(`sysid compid NAME VALUE TYPE`) formats can be loaded. Loaded values become unwritten edits;
press `w` to send them to the vehicle.

## Localisation

GNU gettext is used (libintl is part of glibc). The language comes from `LANG` / `LC_MESSAGES`:

```sh
LANG=uk_UA.UTF-8 ./build/fpl   # Ukrainian
LANG=C ./build/fpl             # English
```

- `po/LINGUAS`: list of languages, `po/<lang>.po`: translations, `po/fpl.pot`: template.
- The build compiles `.mo` files into `build/locale/`, so translations work from the build directory without
  installing. `make install` puts them into `share/locale`. `FPL_LOCALEDIR` overrides the directory.
- After changing strings in the code run `cmake --build build --target pot`. It updates the template and merges
  the new strings into every `.po`. Poedit or Weblate work well for translating.
- New language: `msginit -i po/fpl.pot -l <lang> -o po/<lang>.po`, then add it to `po/LINGUAS`.
- In the code: `_("…")` translates a string, `N_("…")` marks strings in static tables, `ngettext()` handles
  plurals. Pitfall: `_("")` returns the catalog header, so empty strings are never translated.
- Not translated: key names, the debug log, the TSV header of `--dump`, and parameter descriptions from the
  metadata (see "Translated parameter descriptions" above for those).
- Without gettext: `-DFPL_NLS=OFF`.

## Testing without hardware

`build/fakevehicle` is a minimal vehicle: HEARTBEAT, AUTOPILOT_VERSION, the parameter protocol and MAVFTP
(`@PARAM/param.pck` with defaults, like ArduPilot). A reboot makes it silent for `--boot-time` seconds (over TCP the
connection goes away, like a USB port), then report `MAV_STATE_BOOT` for a while; `STAT_BOOTCNT` counts the boots.

```sh
build/fakevehicle --loss 20 --count 1500     # ArduPilot over UDP → 127.0.0.1:14550, 20% loss
build/fakevehicle --px4 --tcp 5760           # PX4 (byte-wise) as a TCP server
build/fakevehicle --pty                      # prints /dev/pts/N — emulates a serial port
build/fakevehicle --no-ftp                   # no MAVFTP: exercises the PARAM_REQUEST_LIST fallback
build/fakevehicle --armed                    # armed: a reboot is refused
```

`tools/ci/smoke.sh [BUILD_DIR]` downloads the parameters with `fpl --dump` from it over MAVFTP, over
`PARAM_REQUEST_LIST`, with 30% packet loss and as PX4, and checks that every path gives the same values
(CI runs it on an ASan build).

PX4 SITL: `docker run --rm -it --network host px4io/px4-sitl:latest`, then just `fpl`.

## Layout

```
src/main.c        command line
src/app.c         poll() main loop: detection → session → metadata → UI
src/link.c        transports: serial (termios), UDP in/out, TCP; MAVLink parser per link
src/detect.c      auto-detection (sysfs, VID/PID, UDP 14550, TCP 5760)
src/vehicle.c     heartbeat, AUTOPILOT_VERSION, parameter protocol
src/ftp.c         MAVFTP client (file download)
src/parampck.c    ArduPilot @PARAM/param.pck parser
src/params.c      parameter store, C-cast / byte-wise encoding, groups, defaults
src/meta.c        apm.pdef.xml (expat) and PX4 JSON (cJSON) parsers, metadata translations
src/metafetch.c   background metadata download and cache (libcurl, pthread)
src/paramfile.c   MP/QGC parameter files
src/favs.c        favourites
src/ui*.c         notcurses UI: table, editors, groups, dialogs
src/i18n.c        gettext: locating the catalogs
po/               UI translations (gettext)
translations/     machine translations of PX4 and ArduPilot metadata
tools/fakevehicle.c
tools/px4-translate/, tools/ardupilot-translate/   how the metadata translations were made
tools/pkg/        deb and rpm builds in containers, install test
tools/pkg/arch/   PKGBUILD for Arch Linux, local build with makepkg
tools/ci/smoke.sh smoke test against fakevehicle
.github/workflows/ CI and releases
```

## Limitations / next steps

- PX4: metadata comes from QGC master, not from the vehicle itself (component metadata over MAVFTP is not
  implemented yet).
- Serial ports are enumerated on Linux only (sysfs); the transport code itself is POSIX.
- notcurses waits for the terminal's reply to a DA1 query, so it doesn't start inside a *detached* tmux with no
  client. It works in a normal terminal and in tmux with an attached client.

## License

[MIT](LICENSE). Dependencies have their own licenses: notcurses (Apache-2.0), MAVLink c_library_v2 (MIT),
cJSON (MIT), expat (MIT), libcurl (curl license).
