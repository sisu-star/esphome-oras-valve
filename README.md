# Oras smart valve → Home Assistant

Control an Oras Bluetooth shutoff valve from Home Assistant using a ~€14
ESP32 board. No soldering, no wiring, nothing opened up — the ESP32 just
talks to the valve over Bluetooth the same way the press pad does.

You get:

- a **switch** that opens and closes the valve
- a **binary sensor** showing the valve's real state, read from the valve
  itself, so it stays correct even when someone uses the physical pad
- an **adjustable open duration** — the pad offers 4 or 12 hours, you get
  anything from 5 minutes to 12 hours
- the press pad keeps working exactly as before

What you do with it is up to you: dishwasher and washing machine cycles,
garden irrigation, closing the water when nobody is home. This project only
provides the control.

> **Status.** Working and in daily use, but developed against one
> installation: a single Oras 272100 with one control unit and one press pad.
> It may well work on other Oras Bluetooth valves — if you try one, please
> open an issue either way.

---

## How it works

```
Home Assistant
     │  WiFi
     ▼
   ESP32        ← a small board on a USB charger, in or near the kitchen
     │  Bluetooth LE
     ▼
Control unit → valve
```

The ESP32 listens for the control unit's state broadcasts and reports them to
Home Assistant. To control the valve, it transmits the same Bluetooth
advertisement the press pad sends.

Nothing is physically connected to the valve, and the ESP32 does not sit
between Home Assistant and the valve — it does the Bluetooth work itself.
Home Assistant talks to it over normal WiFi.

The full protocol is documented in [PROTOCOL.md](PROTOCOL.md) if you want to
build your own implementation on different hardware.

---

## What you need

| Item | Notes |
| --- | --- |
| An Oras Bluetooth valve | control unit + press pad, already installed and paired |
| An ESP32 board | ESP32-WROOM-32 or similar, ~€10–15 |
| A micro-USB **data** cable | charge-only cables will not work |
| A USB power supply | for the ESP32's permanent home |
| A computer | Linux, macOS or Windows, to flash the board |

Any plain ESP32 dev board works. Avoid boards with tiny ceramic antennas
(e.g. the ESP32-C3 SuperMini) — antenna quality matters here, since the
control unit usually lives inside a cabinet.

This project uses the classic ESP32. Other variants (S3, C3, C6) should work
with a different `board:` value, but are untested.

---

## Step 1 — Install ESPHome

If you already run the ESPHome add-on in Home Assistant you can use that
instead; just create the devices there and paste the YAML in.

Otherwise, on your computer:

```bash
python3 -m venv ~/esphome-venv
~/esphome-venv/bin/pip install esphome
```

On Debian or Ubuntu you may first need:

```bash
sudo apt install python3-venv python3-full
```

Check it works:

```bash
~/esphome-venv/bin/esphome version
```

**On Linux, add yourself to the serial port group**, then log out and back in:

```bash
sudo usermod -aG dialout $USER
```

---

## Step 2 — Get the files and set up secrets

```bash
git clone https://github.com/sisu-star/esphome-oras-valve.git
cd esphome-oras-valve
cp secrets.yaml.example secrets.yaml
```

Edit `secrets.yaml` with your WiFi details, and generate an API key:

```bash
openssl rand -base64 32
```

Paste that as `api_encryption_key`.

All commands below are run from this folder, so the config files, the header
and `secrets.yaml` sit together.

---

## Step 3 — Discover your valve's values

Three values are unique to your installation and cannot be guessed. The
discovery firmware finds them for you.

**Plug the ESP32 into your computer**, then:

```bash
~/esphome-venv/bin/esphome run oras-discovery.yaml
```

The first compile takes 5–15 minutes — it is building the whole ESP-IDF
framework. Later builds take under a minute.

Pick the serial port when asked (`/dev/ttyUSB0` on Linux). When it finishes it
shows live logs. **Leave that window open.**

Now carry the board **close to your control unit** — the box on the valve
itself, usually under the sink. Within a few seconds you should see:

```
[discovery]: CONTROL UNIT  mac=XX:XX:XX:XX:XX:XX  valve=CLOSED  serial=...
```

**Then press the physical press pad once.** That produces:

```
[discovery]: PAD COMMAND   mac=XX:XX:XX:XX:XX:XX  cmd=open  seq=3  duration=14400s  pair_id=XXXXXXXX
```

and finally the block you need:

```
=====================================================
  ALL VALUES FOUND — paste this into your config:

  substitutions:
    unit_mac: "XX:XX:XX:XX:XX:XX"
    pad_mac:  "XX:XX:XX:XX:XX:XX"
    pair_id:  "XXXXXXXX"
=====================================================
```

**Copy those three lines.** You cannot get `pair_id` any other way — it only
exists inside a frame the pad transmits while being pressed.

This firmware only listens. It cannot move the valve.

### If nothing appears

- Get closer to the control unit. Signal is the usual problem.
- Press the pad — the pad is silent unless pressed.
- Check the board joined WiFi (look for an IP address in the log).

---

## Step 4 — Flash the control firmware

Open `oras-valve.yaml` and replace the three placeholder substitutions near
the top with your values from step 3. Leave everything else alone.

```yaml
substitutions:
  name: oras-valve
  friendly_name: Oras Valve

  unit_mac: "XX:XX:XX:XX:XX:XX"
  pad_mac:  "XX:XX:XX:XX:XX:XX"
  pair_id:  "XXXXXXXX"

  default_duration: "14400"
```

Then flash it:

```bash
~/esphome-venv/bin/esphome run oras-valve.yaml
```

Watch the log for a clean boot: WiFi connected, and a `UNIT CLOSED` or
`UNIT OPEN` line. No `TX setup failed` warnings.

---

## Step 5 — Add it to Home Assistant

**Settings → Devices & Services.** The device should appear as discovered —
click Add and paste the same `api_encryption_key` from your `secrets.yaml`.

You now have:

| Entity | What it does |
| --- | --- |
| `switch.*_valve` | opens and closes the valve |
| `binary_sensor.*_valve_open` | the valve's real state, from the valve |
| `number.*_open_duration` | how long an open command lasts, in seconds |
| `sensor.*_control_unit_signal` | signal strength — use this for placement |
| `sensor.*_press_pad_battery_raw` | pad battery, raw value (see PROTOCOL.md) |

**Test it:** toggle the switch on, off, then on again, watching the log. All
three should take effect. If you are near the valve you will hear it open.

---

## Step 6 — Find it a permanent home

Unplug from the computer and plug into a USB power supply near the valve.

Check `sensor.*_control_unit_signal` in Home Assistant:

- **−70 dBm or better** — good
- **−80 dBm** — works, but will be fragile once a cabinet door closes
- **worse than −85 dBm** — move it

Only the **control unit** matters for placement. The press pad is irrelevant.

It does not need to be inside the cabinet or have line of sight. A nearby
cupboard, behind a kickboard, or on top of a wall cabinet all work. The power
socket is usually the real constraint.

### Set a DHCP reservation

**Important:** this firmware changes the ESP32's WiFi MAC address, because it
has to clone the press pad's Bluetooth address and both derive from the same
base. Your router will see a new device.

Reserve an IP for the MAC ending **two below** your `pad_mac`. For example,
if `pad_mac` ends `...F9:B2`, reserve the address ending `...F9:B0`.

After that, update as often as you like over WiFi — no cable needed:

```bash
~/esphome-venv/bin/esphome run oras-valve.yaml
~/esphome-venv/bin/esphome logs oras-valve.yaml
```

---

## Things to know

**The press pad still works**, and sharing a Bluetooth address with the ESP32
does not disturb it.

**After Home Assistant changes the valve, the pad's next press goes the wrong
way.** The pad keeps its own idea of the valve's state and toggles it on each
press rather than asking the valve. One press resyncs it; a second does what
you wanted. This is in the pad's firmware and cannot be fixed from here.

**A red light on the pad does not mean the command failed.** The pad has to
*hear* the control unit's confirmation, and its receiving range is much worse
than its transmitting range. The valve often acts correctly while the pad
still shows red.

**The valve closes itself.** Every open command carries a duration, and the
control unit enforces it. That is your safety net if Home Assistant never
sends a close. The default is 4 hours; adjust `number.*_open_duration` to
suit what you are doing.

**Automations are left to you.** This project gives you a switch and a state
sensor; what you build on them depends entirely on your use case.

---

## Troubleshooting

**Nothing found during discovery.** Get closer to the control unit, and press
the pad. The pad only transmits while pressed.

**The switch does nothing.** Check the log for a `TX seq=...` line. If it is
there but the valve does not move, your `pad_mac` or `pair_id` is wrong —
re-run discovery. If there is no `TX` line at all, the switch is not reaching
the script.

**`TX setup failed: cfg=259 start=259`.** Error 259 is "invalid state", the
Bluetooth stack was not ready. Normally only seen if something transmits
during boot.

**The device disappeared after an update.** Its IP may have changed. Try
`--device <name>.local`, or look for its MAC in your router's client list.

**It will not flash over USB.** Free the port first:

```bash
sudo fuser -k /dev/ttyUSB0
```

Unplug and replug the board, then run the flash command again. The moment you
see `Connecting....`, hold **BOOT**, tap **EN**, keep holding BOOT for two
more seconds, then release. That forces the bootloader manually.

**The board stopped responding entirely.** Serial flashing works regardless of
firmware state. Connect by USB and reflash using the BOOT/EN sequence above.

**Do not enable `sram1_as_iram`.** ESPHome suggests it in the log. It has been
observed to cause a silent boot loop on this build, and the extra memory is
not needed.

---

## Files

| File | Purpose |
| --- | --- |
| `oras-discovery.yaml` | step 3 — finds your MACs and pair ID |
| `oras-valve.yaml` | step 4 — the real firmware |
| `oras_includes.h` | C++ headers the lambdas need |
| `secrets.yaml.example` | template for your WiFi and API key |
| `PROTOCOL.md` | the reverse-engineered protocol, for other platforms |

---

## Contributing

Reports from other Oras models are especially valuable — the protocol was
only ever tested against one. If you try this on a different valve, please
open an issue with the model number and whether it worked.

Also of interest: whether `pair_id` is specific to a pad or to a pairing.
Anyone with two pads or two control units could settle that in a few minutes.

---

## Licence

MIT. See [LICENSE](LICENSE).

Not affiliated with or endorsed by Oras. This is independent interoperability
work on hardware the author owns. Use at your own risk — it controls a water
valve, so think about what happens if it fails open.
