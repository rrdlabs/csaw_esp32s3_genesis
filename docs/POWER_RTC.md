# CsAW Power, RTC and Low-Power Behaviour (AGENTS.md §8)

Implementation: `components/csaw_hal/csaw_hal.{c,h}` (device register access) and
`main/power/csaw_power.{c,h}` (policy, wake bookkeeping, cron integration).
All state polled every second is kept in RAM; only mode changes, wake events and
timer scheduling are appended to the SD log in `/sd/csaw/logs/csaw.log`.

## 1. Charging and battery policy (AXP2101 @ `0x34`)

The PMU stage runs before the display (see [HARDWARE_BRINGUP.md](HARDWARE_BRINGUP.md))
and configures the charger before any UI is drawn. Reprogramming is idempotent.

| Register | Bits | Value | Meaning / source |
| --- | --- | --- | --- |
| `BAT_DET_CTRL 0x68` | 0 | 1 | Enable battery detection (XPowersLib `enableBattDetection()`). |
| `CHARGE_GAUGE_WDT_CTRL 0x18` | 3 | 1 | Enable fuel gauge (`enableGauge()`). |
| `CHARGE_GAUGE_WDT_CTRL 0x18` | 2 | 1 | Enable button/RTC backup-cell charging (`enableButtonBatteryCharge()`). |
| `BTN_BAT_CHG_VOL_SET 0x6A` | 2:0 | `4` | 3000 mV = 2600 mV + 4 × 100 mV (`CSAW_BACKUP_CELL_MV`). |
| `LOW_BAT_WARN_SET 0x1A` | 7:4 | `15` | 15% warning threshold (vendor supports 5–20%). |
| `RESET_FUEL_GAUGE 0x17` | 3 | pulse | One-time gauge reset at 10 ms, then released. |
| `ADC_CHANNEL_CTRL 0x30` | 1 | 0 | NTC/TS measurement disabled (existing vendor init step). |

Live status is read from `STATUS2 0x01[6:5]` (`01` charging, `10` discharging,
`00` standby), `BAT_PERCENT_DATA 0xA4` and `ADC_DATA_RELUST0/1 0x34/0x35`
(vendor H5L8 layout). The UI and `power_status` display percent, voltage,
charging state and backup-cell charging state.

## 2. Energy state machine

| Mode | Brightness | Idle before light sleep | Entry condition |
| --- | --- | --- | --- |
| `active` | 60% | 120 s | Default; charger connected or battery > 30%. |
| `save` | 40% | 30 s | Battery ≤ 30% and discharging. |
| `sleep` | 30% (minimum allowed) | 8 s | Manual `power_mode sleep`. |
| `critical` | 30% (minimum allowed) | 20 s | Battery ≤ 15% and discharging. |

`power_mode <mode>` forces a manual mode; automatic battery thresholds are
suspended until another manual call or reboot. Automatic transitions still log
the reason (`battery-low`, `battery-critical`). The 30% brightness gate from
AGENTS.md 4.2 is never lowered: energy modes clamp to `CSAW_LCD_BRIGHTNESS_MIN`.

Light sleep is entered only in `save`/`sleep` after the idle timeout. Wake
sources:

- **Touch:** FT3168 INT is GPIO21 (vendor `pin_config.h` `TP_INT`); configured
  as an input with pull-up and `GPIO_INTR_LOW_LEVEL` GPIO wake.
- **Shake:** QMI8658 wake-on-motion is programmed with the vendor parameters
  (200 mg threshold, low-power 128 Hz ODR, 0x20-sample blanking, CTRL9 command
  `0x08`, STATUS1 bit 2). The exact QMI INT-to-ESP GPIO is **not established**
  on Rev 1; rather than guess a pin, the service uses a 500 ms light-sleep
  timer wake and polls the latched WoM flag. This is documented as a board
  limitation, not a hardware guess.
- **Wake word:** `csaw_power_notify_wake_word()` is the wired entry point for
  the audio front-end. It injects `Wake word detected: <phrase>` onto the
  `system` channel of the same inbound message bus used by the on-screen UI,
  so scheduled/automatic task execution uses the existing agent loop. The CLI
  command `wake_word "<phrase>"` exercises the hook today; the acoustic
  recognizer itself is staged.

## 3. PCF85063A RTC alarms and timers

### Absolute alarms (cron)

`cron_add_job()` and `cron_service_start()` call `csaw_power_rtc_set_alarm()`
with the earliest enabled job's `next_run`. The power task refreshes this
alarm when the earliest job changes. Programming uses BCD registers
`0x0B..0x0F`; disabled fields use the vendor `NO_ALARM` value `0x80`. Control_2
bit 7 (AIE) is enabled after clearing bit 6 (AF). When AF is observed, the
service consumes the flag, disables AIE and calls
`cron_service_process_due_now()` so the cron message is injected at the
realized time.

The PCF85063A alarm has no month/year field; recurring jobs are re-armed from
cron's `next_run` after each firing. Multi-day scheduling therefore relies on
cron's epoch bookkeeping plus RTC re-arming, not on a single hardware alarm
register.

### Countdown timers

`rtc_timer <seconds> "<message>"` creates a cron `AT` job at `now + seconds`
and arms the PCF85063A countdown timer:

- 1–255 s: `Timer_value 0x10 = seconds`, `Timer_mode 0x11 = TCF 1 Hz (0x10) | TE (0x04)`.
- 60–15300 s: `TCF 1/60 Hz (0x18) | TE`, `Timer_value = seconds / 60`.
- Longer spans: cron job only, plus an absolute RTC alarm.

The timer flag is `Control_2` bit 3 (TF) per NXP datasheet §8.2.2/§8.6. The INT
output is not used; the flag is polled and cleared by the power task. NXP's
recommended sequence (disable TE before loading `Timer_value`) is followed.
On expiry the service calls `cron_service_process_due_now()` and re-arms the
next cron alarm.

## 4. CLI reference

| Command | Purpose |
| --- | --- |
| `power_status` | Battery, charging, mode, wake source, WoM state, RTC trace, sleep cycles. |
| `power_mode <active\|save\|sleep\|critical>` | Force an energy mode. |
| `rtc_time` | Read the PCF85063A calendar. |
| `rtc_alarm_clear` | Clear AF/AIE. |
| `rtc_timer <seconds> "<message>"` | Schedule a cron task with an RTC countdown. |
| `wake_word "<phrase>"` | Inject a wake-word event into the agent loop. |

On-screen status shows a compact `BAT n% CHG|DIS|STBY | mode | wake=...` line
every 15 seconds through `csaw_ui_show_status()`.

## 5. Source evidence

- Waveshare XPowersLib `AXP2101Constants.h` and `XPowersAXP2101.tpp`
  (registers, gauge, backup charging, percentage/ADC reads, charge status).
- Waveshare `SensorPCF85063.hpp`, `PCF85063Constants.h`,
  `PCF85063_AlarmByUnits.ino` (BCD alarm format, AIE/AF bits).
- NXP PCF85063A datasheet Rev. 7, §§8.2.2, 8.5, 8.6 (Control_2 TF, alarm and
  timer mode bits, countdown reload).
- Waveshare `SensorQMI8658.hpp`, `QMI8658Constants.h`, `QMI8658_WakeOnMotion.ino`
  (WOM threshold/ODR/CTRL9/STATUS1 sequence).
- Waveshare Rev 1 schematic and `pin_config.h` (`TP_INT = GPIO21`, QMI INT nets
  present but not tied to a documented ESP GPIO).
