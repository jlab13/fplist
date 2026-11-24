# gndtui

Термінальний (TUI) редактор параметрів для **ArduPilot** і **PX4** через MAVLink.
Таблиця з колонками як у Mission Planner «Full Parameter List»:
`Command | Value | Units | Options | Desc | Fav`.

- Plain C (C11), UI на [notcurses](https://github.com/dankamongmen/notcurses),
  протокол — [mavlink/c_library_v2](https://github.com/mavlink/c_library_v2) (діалект `ardupilotmega`).
- Автопошук апарата як у QGroundControl, або примусове підключення з командного рядка.
- Завантаження параметрів з дозапитом пропущених індексів, запис з повторами й перевіркою відповіді.
- Метадані (одиниці, діапазони, enum, бітові маски, описи) завантажуються автоматично і кешуються.

## Збірка

Потрібні: CMake ≥ 3.20, C-компілятор, `pkg-config`, `libcurl`, `expat`, `ncursesw`,
`libunistring`, `libdeflate` (на Arch: `pacman -S curl expat ncurses libunistring libdeflate`).

```sh
cmake -S . -B build
cmake --build build -j
./build/gndtui
```

MAVLink і cJSON завантажуються CMake (зафіксовані версії). Якщо в системі є
`notcurses-core` ≥ 3.0, береться він; інакше notcurses 3.0.17 збирається статично в `build/`
(без мультимедіа і C++). Примусово використати вбудований: `-DGNDTUI_SYSTEM_NOTCURSES=OFF`.
Збірка з санітайзерами: `-DGNDTUI_ASAN=ON`.

## Запуск

```sh
gndtui                          # автопошук
gndtui /dev/ttyACM0             # USB (швидкість для CDC не важлива)
gndtui /dev/ttyUSB0:57600       # телеметрійний радіомодем
gndtui udp:14550                # слухати UDP (PX4 SITL, companion, ESP-телеметрія)
gndtui udpout:192.168.4.1:14550 # надсилати на конкретну адресу
gndtui tcp:127.0.0.1:5760       # ArduPilot SITL
gndtui --list                   # показати знайдені серійні порти і їх класифікацію
gndtui --dump > params.tsv      # без UI: вивантажити параметри в TSV і вийти
```

Інші опції: `gndtui --help`.

### Автопошук

Як `LinkManager` у QGroundControl:

- **USB**: перебір `ttyACM*`/`ttyUSB*` через sysfs, класифікація за USB VID/PID
  (3DR/PX4, ArduPilot ChibiOS `1209:5740/5741`, CubePilot, Holybro, CUAV, ARK, NXP, STM32 VCP,
  SiK-радіо на FTDI/CP210x). Невідомі `ttyACM*` теж пробуються, невідомі USB-UART (`ttyUSB*`) ні.
  Порти в режимі бутлоадера пропускаються; після появи порту витримується 1 с.
  Для радіомодемів, якщо нічого не розпізнано, швидкість перемикається між 57600 і 115200.
- **UDP**: прослуховування `0.0.0.0:14550`.
- **TCP**: спроба `127.0.0.1:5760` (ArduPilot SITL; цього QGC не робить, Mission Planner так).

На всі кандидати надсилається GCS HEARTBEAT 1 Гц (PX4 вмикає MAVLink на USB лише після
вхідного трафіку). Перший канал, з якого прийшов HEARTBEAT автопілота, перемагає.
Якщо зв'язок обірвався, канал перевідкривається щосекунди, а таблиця і незаписані зміни зберігаються.

Якщо `/dev/ttyACM*` недоступний, на екрані пошуку буде підказка, до якої групи себе додати
(на Arch це `uucp`).

## Протокол параметрів

- `PARAM_REQUEST_LIST` (повтор до 4 разів, якщо апарат мовчить), далі пропущені індекси
  дозапитуються `PARAM_REQUEST_READ` пачками по 32. Наступна пачка йде одразу, щойно прийшла
  попередня, або після 0.6 с тиші на повільних каналах.
- Кодування значень: ArduPilot — C-cast, PX4 — byte-wise. Визначається за `AUTOPILOT_VERSION.capabilities`
  (`MAV_CMD_REQUEST_MESSAGE`, запасний варіант `REQUEST_AUTOPILOT_CAPABILITIES`), а до відповіді — за типом автопілота.
- Запис: `PARAM_SET`, до 8 одночасно, підтвердження — луна `PARAM_VALUE` з тим самим значенням,
  4 спроби з тайм-аутом 1.2 с. Якщо апарат повернув інше значення, це показується як помилка.
- `STATUSTEXT` від польотного контролера показується в рядку стану.

## Метадані

| Прошивка | Джерело | Кеш |
|---|---|---|
| ArduPilot | `autotest.ardupilot.org/Parameters/versioned/<Copter\|Plane\|Rover\|Sub\|Tracker>/stable-X.Y.Z/apm.pdef.xml` для офіційних релізів, інакше `…/Parameters/<ArduCopter\|…>/apm.pdef.xml` (master) | назавжди для версійних, 7 днів для master |
| PX4 | `PX4ParameterFactMetaData.json` з репозиторію QGroundControl | 7 днів |

Кеш лежить у `$XDG_CACHE_HOME/gndtui` (`~/.cache/gndtui`). `--offline` не завантажує нічого,
`--meta FILE` бере свій `apm.pdef.xml` або PX4 `.json`.

## Клавіші

| Клавіша | Дія |
|---|---|
| `↑↓` `j k` `PgUp PgDn` `Home End` `g G`, колесо миші | навігація |
| `Enter` / `e` / подвійний клік | редагувати значення: для enum список, для бітових масок чекбокси, інакше текст (`↑↓` змінюють на крок `Increment`) |
| `w` | записати змінені параметри в апарат (як «Write Params» у MP) |
| `r` | перечитати всі параметри |
| `z` / `Z` | скасувати зміну поточного / всіх |
| `/` | пошук за назвою та описом (збіги в назві першими), `Esc` очищає |
| `m` | лише змінені |
| `f` / `Space` / клік у колонці Fav | обране (обране сортується першим, зберігається в `~/.config/gndtui/favorites`) |
| `v` | багаторядкові рядки, як у Mission Planner |
| `d` | панель деталей (тип, діапазон, крок, default, reboot required, повний опис) |
| `s` / `l` | зберегти / завантажити файл параметрів |
| `?` | довідка |
| `q` / `Ctrl-C` | вихід (з підтвердженням, якщо є незаписані зміни) |

Значення поза рекомендованим діапазоном вимагають повторного `Enter`.
Read-only параметри не редагуються.

Файли: зберігаються у форматі Mission Planner (`NAME,VALUE`). Завантажуються формати Mission Planner і
QGroundControl (`sysid compid NAME VALUE TYPE`). Завантажені значення стають незаписаними змінами;
щоб відправити їх в апарат, натисніть `w`.

## Тестування без заліза

`build/fakevehicle` — мінімальний апарат: HEARTBEAT, AUTOPILOT_VERSION, протокол параметрів.

```sh
build/fakevehicle --loss 20 --count 1500     # ArduPilot через UDP → 127.0.0.1:14550, 20% втрат
build/fakevehicle --px4 --tcp 5760           # PX4 (byte-wise) як TCP-сервер
build/fakevehicle --pty                      # друкує /dev/pts/N — емуляція серійного порту
```

PX4 SITL: `docker run --rm -it --network host px4io/px4-sitl:latest`, потім просто `gndtui`.

## Структура

```
src/main.c        аргументи командного рядка
src/app.c         головний цикл poll(): пошук → сесія → метадані → UI
src/link.c        транспорти: serial (termios), UDP in/out, TCP; парсер MAVLink на канал
src/detect.c      автопошук (sysfs, VID/PID, UDP 14550, TCP 5760)
src/vehicle.c     heartbeat, AUTOPILOT_VERSION, протокол параметрів
src/params.c      сховище параметрів, кодування C-cast / byte-wise
src/meta.c        парсери apm.pdef.xml (expat) і PX4 JSON (cJSON)
src/metafetch.c   фонове завантаження і кешування метаданих (libcurl, pthread)
src/paramfile.c   файли параметрів MP/QGC
src/favs.c        обране
src/ui*.c         notcurses UI: таблиця, редактори, діалоги
tools/fakevehicle.c
```

## Обмеження / що далі

- ArduPilot по USB віддає параметри повільно (~60/с, близько 18 с на 1100 параметрів), бо сам обмежує
  потік. Mission Planner для цього використовує MAVFTP `@PARAM/param.pck` (менше секунди); це наступний
  кандидат на реалізацію.
- PX4: метадані з master QGC, а не з самого апарата (component metadata через MAVFTP поки не реалізовано).
- Серійні порти перебираються лише в Linux (sysfs); сам код транспорту POSIX.
- notcurses чекає відповіді терміналу на запит DA1, тому всередині *detached* tmux без клієнта не стартує.
  У звичайному терміналі та в tmux із підключеним клієнтом працює.
