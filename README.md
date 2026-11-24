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
`libunistring`, `libdeflate` (on Arch: `pacman -S curl expat ncurses libunistring libdeflate`).

```sh
cmake -S . -B build
cmake --build build -j
./build/fpl
```

CMake downloads MAVLink and cJSON (pinned versions). If the system has `notcurses-core` ≥ 3.0
it is used; otherwise notcurses 3.0.17 is built statically inside `build/` (no multimedia, no C++).
To force the bundled one: `-DFPL_SYSTEM_NOTCURSES=OFF`. Sanitizer build: `-DFPL_ASAN=ON`.

### Debian package

`tools/deb/build.sh` builds the `fpl` package (and `fpl-dbgsym` with the symbols for gdb) for Debian 13
(trixie) in a container, so it works the same on any distribution. The packages go to `dist/<arch>/`:

```sh
tools/deb/build.sh               # for the architecture of this machine
tools/deb/build.sh arm64         # amd64 or arm64; another architecture runs under QEMU
sudo apt install ./dist/amd64/fpl_*.deb
```

- **What you need:** Podman, Docker or Apple `container` (the first one found; `CONTAINER_ENGINE=` picks
  one). For another architecture you also need QEMU user emulation
  (`pacman -S qemu-user-static qemu-user-static-binfmt`, `apt install qemu-user-binfmt`).
  The build downloads MAVLink, cJSON and notcurses, so it needs the network.
- **What's inside:** Debian 13 has no notcurses, so it is linked statically. The other libraries
  (curl, expat, ncurses, libunistring, libdeflate) become dependencies through `dpkg-shlibdeps`.
- **Version:** the one in `project()` of `CMakeLists.txt`, e.g. `fpl_0.1.0_amd64.deb`. To install a new
  build over an old one, raise it.
- **Maintainer:** taken from `git config user.name` / `user.email` of whoever builds the package.
- Common settings are in `cmake/DebPackage.cmake`, and the container is in `tools/deb/`.

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
(`@PARAM/param.pck` with defaults, like ArduPilot).

```sh
build/fakevehicle --loss 20 --count 1500     # ArduPilot over UDP → 127.0.0.1:14550, 20% loss
build/fakevehicle --px4 --tcp 5760           # PX4 (byte-wise) as a TCP server
build/fakevehicle --pty                      # prints /dev/pts/N — emulates a serial port
build/fakevehicle --no-ftp                   # no MAVFTP: exercises the PARAM_REQUEST_LIST fallback
```

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
tools/deb/        Debian package build in a container
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
