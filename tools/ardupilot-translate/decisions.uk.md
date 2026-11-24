# Project terminology decisions (in addition to uk_terms.md)
- swash plate → автомат перекосу
- range finder / range sensor / distance sensor → вимірювач дальності (as in approved PX4 translations)
- airframe → планер (vehicle geometry/type); multicopter frame → рама
- bias (sensor) → зміщення
- innovation gate / gate size → ворота інновацій / розмір воріт інновацій
- flare (landing) → вирівнювання перед приземленням
- failure (hardware/motor/sensor) → відмова; software fault → помилка
- dead-reckoning → сліпа навігація (as in approved PX4 translations)
- actuator → виконавчий пристрій; servo → сервопривід; rotor → ротор; main rotor → несний гвинт
- tiltrotor → конвертоплан; tailsitter → тейлсітер; quad-chute → quad-chute (PX4 feature name, Latin)
- calibrated airspeed → калібрована повітряна швидкість (NOT "приладова", that is indicated airspeed)

# ArduPilot specifics
- Flight mode names stay exactly as in English (Latin script, same spelling/case), both as enum labels
  (FLTMODEn, MODE*, *_MODE lists) and when mentioned in text: Stabilize, Acro, AltHold, Auto, Guided,
  Loiter, RTL, Circle, Land, Drift, Sport, Flip, AutoTune, PosHold, Brake, Throw, SmartRTL, FlowHold,
  Follow, ZigZag, Turtle, MANUAL, FBWA, FBWB, CRUISE, TRAINING, QSTABILIZE, QHOVER, QLOITER, QLAND,
  QRTL, QACRO, THERMAL, HOLD, STEERING, SURFACE, DEPTH HOLD, POSHOLD, etc. Generic words around them
  are translated ("режим Loiter").
- Firmware / vehicle names stay: ArduPilot, Copter, Plane, Rover, Sub, Blimp, AntennaTracker, QuadPlane.
- Protocol, device, product and library names stay in Latin script: MAVLink2, FrSky, SBUS, CRSF, DroneCAN,
  Lua, OSD, ADSB, Pixhawk, Mission Planner, DCM, EKF3, AHRS, SITL, etc. ("скрипт Lua", "порт DroneCAN").
- pre-arm checks / arming checks → перевірки перед взведенням; PreArm (message prefix) stays.
- GCS → GCS (наземна станція керування only when spelled out as "ground control station").
- centi-degrees → сантиградуси; centimeters → сантиметри (units inside text); keep unit symbols (cdeg, cm/s).
- "Note:" → "Примітка:"; "Warning"/"WARNING" → "Попередження"/"ПОПЕРЕДЖЕННЯ".
- notch filter → режекторний фільтр; harmonic notch → гармонічний режекторний фільтр
- trim (vehicle attitude/airspeed) → балансування; trim value of an RC channel / servo → трим
- rally point → точка збору; stall → звалювання; collective (heli) → колективний крок
- Keep source typos in identifiers exactly (e.g. SYDID_THISMAV stays SYDID_THISMAV).
- (landing) approach → захід на посадку (established aviation term; exception to landing → приземлення); never the Russianism "заход"
