# Full Parameter List (`fpl`)

[English](README.md) · **Українська**

Термінальний (TUI) редактор параметрів для **ArduPilot** і **PX4** через MAVLink. Команда — `fpl`.
Таблиця з колонками як у Mission Planner «Full Parameter List»:
`Command | Value | Units | Options | Desc | Fav`.

- Plain C (C11), UI на [notcurses](https://github.com/dankamongmen/notcurses),
  протокол — [mavlink/c_library_v2](https://github.com/mavlink/c_library_v2) (діалект `ardupilotmega`).
- Автопошук апарата як у QGroundControl, або примусове підключення з командного рядка.
- Завантаження параметрів з дозапитом пропущених індексів, запис з повторами й перевіркою відповіді.
- Метадані (одиниці, діапазони, enum, бітові маски, описи) завантажуються автоматично і кешуються.

## Збірка

Потрібні: CMake ≥ 3.20, C-компілятор, `pkg-config`, `libcurl`, `expat`, `ncursesw`,
`libunistring`, `zlib` (на Arch: `pacman -S curl expat ncurses libunistring zlib`).

```sh
cmake -S . -B build
cmake --build build -j
./build/fpl
```

MAVLink завантажується CMake (зафіксована ревізія). Якщо в системі є
`notcurses-core` ≥ 3.0, береться він; інакше notcurses 3.0.17 збирається статично в `build/`
(без мультимедіа і C++). Так само cJSON: системний `libcjson` ≥ 1.7.19, інакше вбудований 1.7.19.
Примусово використати вбудовані: `-DFPL_SYSTEM_NOTCURSES=OFF`, `-DFPL_SYSTEM_CJSON=OFF`.
Збірка з санітайзерами: `-DFPL_ASAN=ON`.

### Пакети (deb, rpm)

`tools/pkg/build.sh` збирає пакети в контейнері, тож однаково на будь-якому дистрибутиві. Вони лягають у
`dist/<arch>/`:

| | deb | rpm |
|---|---|---|
| Збирається на | Debian 13 | AlmaLinux 10 |
| Файли | `fpl_0.1.0_amd64.deb`, `fpl-dbgsym_…ddeb` | `fpl-0.1.0-1.x86_64.rpm`, `fpl-debuginfo-…rpm` |
| Ставиться на | Debian 13, Ubuntu 24.04 і новіші | Fedora, openSUSE Leap 16 / Tumbleweed, RHEL / AlmaLinux / Rocky 10 |

```sh
tools/pkg/build.sh deb             # під архітектуру цієї машини
tools/pkg/build.sh rpm arm64       # amd64 або arm64; чужа архітектура йде під QEMU
tools/pkg/install-test.sh rpm      # поставити й запустити в чистих контейнерах цих систем

sudo apt install ./dist/amd64/fpl_*.deb
sudo dnf install ./dist/amd64/fpl-0*.rpm      # на openSUSE: zypper install --allow-unsigned-rpm
```

- **Що потрібно:** Podman, Docker або Apple `container` (береться перший знайдений; `CONTAINER_ENGINE=`
  вибирає явно). Для чужої архітектури потрібна ще емуляція QEMU
  (`pacman -S qemu-user-static qemu-user-static-binfmt`, `apt install qemu-user-binfmt`).
  Збірка завантажує MAVLink, cJSON і notcurses, тож потрібна мережа.
- **Що всередині:** notcurses вшитий статично (у Debian 13 його немає). Решта бібліотек (curl, expat,
  ncurses, libunistring, zlib) стають залежностями: у deb через `dpkg-shlibdeps`, у rpm як soname
  (`libcurl.so.4()(64bit)`, …). Soname не залежить від назви пакета, тому один rpm підходить усім
  RPM-дистрибутивам. У AlmaLinux 10 найстаріша glibc серед них (2.39), тож rpm не вимагає новішої.
- **Портабельність вбудованого notcurses:** він збирається з zlib замість libdeflate (у RHEL 10 libdeflate
  є лише в EPEL), а його `del_curterm(cur_term)` замінюється на `del_curterm(set_curterm(NULL))`. openSUSE
  збирає ncurses з reentrant ABI, де `cur_term` — функція, і бінарник, зібраний деінде, зупинився б там з
  «undefined symbol: cur_term» (`cmake/Notcurses.cmake`).
- **Версія:** та, що в `project()` у `CMakeLists.txt`. Щоб нова збірка ставилася поверх старої, версію треба
  підняти. Реліз rpm (`-1`) рахує пакування однієї версії.
- **Maintainer** (deb) / **Vendor** (rpm): `$DEB_MAINTAINER`, якщо задано (CI використовує noreply-адресу
  автора на GitHub), інакше `git config user.name` / `user.email` того, хто збирає пакет.
- Налаштування: `cmake/DebPackage.cmake`, `cmake/RpmPackage.cmake`; контейнери і скрипти: `tools/pkg/`.

### Пакет для Arch Linux

`tools/pkg/arch/PKGBUILD` збирає з релізного архіву GitHub: notcurses і cJSON з `[extra]`, MAVLink —
джерело самого PKGBUILD; `check()` запускає `tools/ci/smoke.sh`. На Arch `tools/pkg/arch/build.sh`
збирає його з робочого дерева (без контейнера) у `dist/arch/` і перевіряє `namcap`:

```sh
sudo pacman -S --needed base-devel cmake notcurses cjson curl expat namcap
tools/pkg/arch/build.sh
sudo pacman -U dist/arch/fpl-0.1.0-1-x86_64.pkg.tar.zst
```

Контрольна сума релізного архіву — `SKIP` до релізу, з яким пакет публікуватиметься (тоді
`updpkgsums`); `pkgver` має дорівнювати `project()` у `CMakeLists.txt`.

### CI і релізи

GitHub Actions (`.github/workflows/ci.yml`) запускається на кожен push і pull request:

- перевірка форматування `clang-format` 23.1.3;
- ASan-збірка з `-Werror` на Ubuntu 24.04 і smoke-тест `tools/ci/smoke.sh`;
- deb і rpm для amd64 і arm64 (на справжньому ARM-раннері, без QEMU) через `tools/pkg/build.sh`, а потім
  `tools/pkg/install-test.sh`: пакети ставляться й запускаються на Debian 13, Ubuntu 24.04 і 26.04 (deb) та
  на Fedora, AlmaLinux 10, openSUSE Leap 16.0 і Tumbleweed (rpm). Пакети зберігаються як артефакти
  запуску.

Реліз — це тег. Підніміть `project(fpl VERSION …)` у `CMakeLists.txt`, закомітьте, тоді:

```sh
git tag v0.2.0 && git push origin v0.2.0
```

Коли всі перевірки пройшли, workflow публікує GitHub Release з пакетами deb і rpm для обох архітектур та
нотатками, згенерованими з комітів. Тег має збігатися з версією в `CMakeLists.txt`, інакше релізу не буде.

## Запуск

```sh
fpl                          # автопошук
fpl /dev/ttyACM0             # USB (швидкість для CDC не важлива)
fpl /dev/ttyUSB0:57600       # телеметрійний радіомодем
fpl udp:14550                # слухати UDP (PX4 SITL, companion, ESP-телеметрія)
fpl udpout:192.168.4.1:14550 # надсилати на конкретну адресу
fpl tcp:127.0.0.1:5760       # ArduPilot SITL
fpl --list                   # показати знайдені серійні порти і їх класифікацію
fpl --dump > params.tsv      # без UI: вивантажити параметри в TSV і вийти
```

Інші опції: `fpl --help`.

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

- **ArduPilot:** параметри завантажуються так само, як у QGroundControl і Mission Planner: одним файлом
  `@PARAM/param.pck?withdefaults=1` через MAVFTP. Це менше 1 с через USB замість ~18 с, і разом зі значеннями
  приходять типові значення з прошивки, з урахуванням плати й рами. Пакети, втрачені посеред потоку, дочитуються
  окремо. Розмір, який ArduPilot повідомляє при відкритті файлу, — лише оцінка, тож кінцем файлу вважається EOF.
  Якщо файлу немає, апарат не відповідає або MAVFTP занадто повільний, використовується звичайний
  `PARAM_REQUEST_LIST`, описаний нижче. `--no-ftp` вимикає MAVFTP.

- `PARAM_REQUEST_LIST` (повтор до 4 разів, якщо апарат мовчить), далі пропущені індекси
  дозапитуються `PARAM_REQUEST_READ` пачками по 32. Наступна пачка йде одразу, щойно прийшла
  попередня, або після 0.6 с тиші на повільних каналах.
- Кодування значень: ArduPilot — C-cast, PX4 — byte-wise. Визначається за `AUTOPILOT_VERSION.capabilities`
  (`MAV_CMD_REQUEST_MESSAGE`, запасний варіант `REQUEST_AUTOPILOT_CAPABILITIES`), а до відповіді — за типом автопілота.
- Запис: `PARAM_SET`, до 8 одночасно, підтвердження — луна `PARAM_VALUE` з тим самим значенням,
  4 спроби з тайм-аутом 1.2 с. Якщо апарат повернув інше значення, це показується як помилка.
- `STATUSTEXT` від польотного контролера показується в рядку стану.

## Типові значення

Типове значення береться з прошивки (ArduPilot, через MAVFTP), а якщо його там немає, то з метаданих (PX4, поле
`default`). Як і в QGC, значення з прошивки має пріоритет. Для PX4 типові значення з метаданих загальні й не
враховують налаштувань airframe, тому у фільтр `n` потрапляє й те, що виставив airframe. Параметри з невідомим
типовим значенням (наприклад, ArduPilot з `--no-ftp`) у фільтр `n` не потрапляють.

## Групи параметрів

Як у QGroundControl, параметри розкладено на два рівні: категорія → група.

- **PX4:** категорія й група беруться з метаданих (`category`, `group`, наприклад «Multicopter Position Control»).
  Назви груп перекладаються, якщо PX4 має їх переклад.
- **ArduPilot:** категорія — поле `User` (Standard / Advanced). Група — частина імені до першого `_` без цифр у кінці,
  тож `SERIAL1_BAUD` і `SERIAL2_PROTOCOL` потрапляють у групу «SERIAL». Групи з одного параметра переносяться в «Різне».
- Категорія «Standard» показується першою, «Other» останньою. Група «Різне» стоїть останньою в категорії.

## Метадані

| Прошивка | Джерело | Кеш |
|---|---|---|
| ArduPilot | `autotest.ardupilot.org/Parameters/versioned/<Copter\|Plane\|Rover\|Sub\|Tracker>/stable-X.Y.Z/apm.pdef.xml` для офіційних релізів, інакше `…/Parameters/<ArduCopter\|…>/apm.pdef.xml` (master) | назавжди для версійних, 7 днів для master |
| PX4 | `PX4ParameterFactMetaData.json` з репозиторію QGroundControl | 7 днів |

Кеш лежить у `$XDG_CACHE_HOME/fpl` (`~/.cache/fpl`). `--offline` не завантажує нічого,
`--meta FILE` бере свій `apm.pdef.xml` або PX4 `.json`.

### Переклад описів параметрів

- **PX4**: використовуються офіційні переклади спільноти з
  [PX4/PX4-Metadata-Translations](https://github.com/PX4/PX4-Metadata-Translations) (Crowdin),
  файл `translated/parameters_<ll_CC>.ts`, кеш 7 днів. Мова береться з `LANGUAGE` / `LC_MESSAGES`,
  а `--meta-lang uk_UA` задає її явно (`--meta-lang en` вимикає переклад). Українською зараз перекладено
  близько 20% рядків (короткі описи, значення enum, біти); решта показується англійською.
- Переклад застосовується, лише якщо його англійський `<source>` збігається з поточним описом.
  Переклад старої версії опису не показується.
- Клавіша `t` перемикає всю таблицю між перекладом і англійським оригіналом, а в заголовку видно `uk_UA` або
  `EN`.
- Пошук шукає і в перекладі, і в оригіналі. `--dump` завжди виводить англійський текст.
- **Машинний переклад PX4.** Рядки, яких немає в перекладі спільноти, заповнює машинний переклад
  `translations/px4/parameters_uk_UA.mt.ts`. Його зроблено Claude Sonnet за правилами й термінологічним листом PX4
  і механічно перевірено, але людина його не переглядала. Переклад спільноти завжди має пріоритет.
  У панелі деталей такі описи позначено «машинний переклад». Як його згенерувати, описано в `tools/px4-translate/`.
- **ArduPilot** офіційних перекладів не має, тому всі описи перекладено машинно:
  `translations/ardupilot/parameters_uk_UA.mt.ts` містить 10 244 унікальні рядки з метаданих master усіх типів
  апаратів (Copter, Plane, Rover, Sub, AntennaTracker, Blimp, Heli) і останніх стабільних версій. Ключем є англійський
  текст, тому переклад підходить будь-якому апарату й версії з тим самим описом, а змінений в ArduPilot опис показується
  англійською. Назви режимів польоту лишаються англійською. Як його згенерувати, описано в `tools/ardupilot-translate/`.
- Доперекласти PX4 можна в їхньому Crowdin, і тоді переклад отримають і користувачі QGroundControl.

## Клавіші

| Клавіша | Дія |
|---|---|
| `↑↓` `PgUp PgDn` `Home End`, колесо миші | навігація |
| `Enter` / `e` / подвійний клік | редагувати значення: для enum список, для бітових масок чекбокси, інакше текст (`↑↓` змінюють на крок `Increment`) |
| `w` | записати змінені параметри в апарат (як «Write Params» у MP) |
| `r` | перечитати всі параметри |
| `z` / `Z` | скасувати зміну поточного / всіх |
| `/` | пошук за назвою та описом (збіги в назві першими), `Esc` очищає |
| `m` | лише змінені в цій сесії (ще не записані або з помилкою запису) |
| `n` | лише відмінні від типових, як «Modified» у QGroundControl. Такі значення підсвічені кольором, а типове значення видно в панелі деталей |
| `g` | групи параметрів як у QGroundControl (категорія → група). Текст, який ви друкуєте, одразу фільтрує список груп (`Esc` очищає фільтр, а якщо він порожній, закриває діалог), `Enter` вибирає, вибрана група стає фільтром таблиці, а `Esc` у таблиці знімає фільтр |
| `f` / `Space` / клік у колонці Fav | обране (обране сортується першим, зберігається в `~/.config/fpl/favorites`) |
| `v` | багаторядкові рядки, як у Mission Planner |
| `d` | панель деталей (тип, діапазон, крок, default, reboot required, повний опис) |
| `t` | описи параметрів: переклад / англійський оригінал |
| `s` / `l` | зберегти / завантажити файл параметрів |
| `?` | довідка |
| `q` / `Ctrl-C` | вихід (з підтвердженням, якщо є незаписані зміни) |

Значення поза рекомендованим діапазоном вимагають повторного `Enter`.
Read-only параметри не редагуються.

Файли: зберігаються у форматі Mission Planner (`NAME,VALUE`). Завантажуються формати Mission Planner і
QGroundControl (`sysid compid NAME VALUE TYPE`). Завантажені значення стають незаписаними змінами;
щоб відправити їх в апарат, натисніть `w`.

## Локалізація

Використовується GNU gettext (libintl входить у glibc). Мова береться з `LANG` / `LC_MESSAGES`:

```sh
LANG=uk_UA.UTF-8 ./build/fpl   # українською
LANG=C ./build/fpl             # англійською
```

- `po/LINGUAS`: список мов, `po/<мова>.po`: переклади, `po/fpl.pot`: шаблон.
- Збірка компілює `.mo` у `build/locale/`, тож з каталогу збірки переклад працює без встановлення.
  `make install` ставить їх у `share/locale`. Каталог можна перевизначити змінною `FPL_LOCALEDIR`.
- Після зміни рядків у коді виконайте `cmake --build build --target pot`. Ця ціль оновить шаблон і зіллє нові
  рядки в усі `.po`. Перекладати зручно в Poedit або Weblate.
- Нова мова: `msginit -i po/fpl.pot -l <мова> -o po/<мова>.po`, потім додайте мову в `po/LINGUAS`.
- У коді: `_("…")` перекладає рядок, `N_("…")` позначає рядки в статичних таблицях, а для множини є `ngettext()`.
  Пастка: `_("")` повертає заголовок каталогу, тому порожні рядки не перекладаємо.
- Не перекладаються: назви клавіш, debug-лог, заголовок TSV у `--dump`, описи параметрів з метаданих
  (ArduPilot і PX4 публікують їх лише англійською).
- Без gettext: `-DFPL_NLS=OFF`.

## Тестування без заліза

`build/fakevehicle` — мінімальний апарат: HEARTBEAT, AUTOPILOT_VERSION, протокол параметрів і MAVFTP
(`@PARAM/param.pck` з типовими значеннями, як у ArduPilot).

```sh
build/fakevehicle --loss 20 --count 1500     # ArduPilot через UDP → 127.0.0.1:14550, 20% втрат
build/fakevehicle --px4 --tcp 5760           # PX4 (byte-wise) як TCP-сервер
build/fakevehicle --pty                      # друкує /dev/pts/N — емуляція серійного порту
build/fakevehicle --no-ftp                   # без MAVFTP: перевірка запасного PARAM_REQUEST_LIST
```

`tools/ci/smoke.sh [КАТАЛОГ_ЗБІРКИ]` завантажує з нього параметри через `fpl --dump`: через MAVFTP, через
`PARAM_REQUEST_LIST`, з 30% втрат пакетів і як PX4, і перевіряє, що всі шляхи дають ті самі значення
(CI запускає його на ASan-збірці).

PX4 SITL: `docker run --rm -it --network host px4io/px4-sitl:latest`, потім просто `fpl`.

## Структура

```
src/main.c        аргументи командного рядка
src/app.c         головний цикл poll(): пошук → сесія → метадані → UI
src/link.c        транспорти: serial (termios), UDP in/out, TCP; парсер MAVLink на канал
src/detect.c      автопошук (sysfs, VID/PID, UDP 14550, TCP 5760)
src/vehicle.c     heartbeat, AUTOPILOT_VERSION, протокол параметрів
src/ftp.c         MAVFTP-клієнт (завантаження файлу)
src/parampck.c    розбір ArduPilot @PARAM/param.pck
src/params.c      сховище параметрів, кодування C-cast / byte-wise, групи, типові значення
src/meta.c        парсери apm.pdef.xml (expat) і PX4 JSON (cJSON)
src/metafetch.c   фонове завантаження і кешування метаданих (libcurl, pthread)
src/paramfile.c   файли параметрів MP/QGC
src/favs.c        обране
src/ui*.c         notcurses UI: таблиця, редактори, групи, діалоги
src/i18n.c        gettext: пошук каталогів перекладів
po/               переклади інтерфейсу (gettext)
translations/     машинні переклади метаданих PX4 і ArduPilot
tools/fakevehicle.c
tools/px4-translate/, tools/ardupilot-translate/   як згенеровано переклади метаданих
tools/pkg/        збірка deb і rpm у контейнерах, тест встановлення
tools/pkg/arch/   PKGBUILD для Arch Linux, локальна збірка makepkg
tools/ci/smoke.sh smoke-тест проти fakevehicle
.github/workflows/ CI і релізи
```

## Обмеження / що далі

- PX4: метадані з master QGC, а не з самого апарата (component metadata через MAVFTP поки не реалізовано).
- Серійні порти перебираються лише в Linux (sysfs); сам код транспорту POSIX.
- notcurses чекає відповіді терміналу на запит DA1, тому всередині *detached* tmux без клієнта не стартує.
  У звичайному терміналі та в tmux із підключеним клієнтом працює.

## Ліцензія

[MIT](LICENSE). Залежності мають власні ліцензії: notcurses (Apache-2.0), MAVLink c_library_v2 (MIT),
cJSON (MIT), expat (MIT), libcurl (curl license).
