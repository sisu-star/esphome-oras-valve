# Oras BLE valve protocol

Reverse engineered from `btmon` captures of a press pad and control unit in
normal use, then verified by transmitting crafted frames and observing the
valve. Everything here is plaintext — there is no encryption anywhere in the
link.

This document exists so the work is reusable outside ESPHome. If you want to
drive the valve from a Raspberry Pi, an nRF52, or anything else that can
transmit a BLE advertisement, this is all you need.

---

## Overview

Both the press pad and the control unit are Silicon Labs devices that
broadcast **legacy connectable advertisements** (`ADV_IND`) containing
manufacturer-specific data under **company ID `0x0131`** (Cypress
Semiconductor). The payload is always **18 bytes**.

There is no connection, no pairing and no GATT involved in normal operation.
The pad broadcasts a command; the unit acts on it and broadcasts its new
state. That is the entire protocol.

Three frame types exist, distinguished by the first payload byte. Conveniently
the advertised device name also differs, which makes frames easy to spot in a
capture:

| Byte 0 | Device | Name | Purpose |
| --- | --- | --- | --- |
| `0x15` | press pad | `ORA5` | command (open / close) |
| `0x14` | press pad | `ORAS` | idle / identity |
| `0x28` | control unit | `ORAS` | valve state |

---

## Command frame — pad to control unit

```
15 5E 00 81 00 68 C0 A8 00 00 00 00 00 00 90 B8 C6 FD
│  │  │  │  │  │  └──┬──┘ └──────┬─────┘ └─────┬─────┘
│  │  │  │  │  │     │           │             └─ pair ID (4 bytes)
│  │  │  │  │  │     │           └─ padding, always zero
│  │  │  │  │  │     └─ duration, uint16 LITTLE-ENDIAN, seconds
│  │  │  │  │  └─ command: 0x68 = open, 0x69 = close
│  │  │  │  └─ always 0x00
│  │  │  └─ sequence: (n << 4) | 0x01, where n is 1..15
│  │  └─ always 0x00
│  └─ battery / voltage proxy
└─ frame type 0x15
```

### Duration

A plain `uint16` in **seconds**, little-endian. The pad's two presets are just
its one-button user interface; the field itself is free.

| Bytes | Value | Meaning |
| --- | --- | --- |
| `40 38` | 14400 | 4 hours (pad's short press) |
| `C0 A8` | 43200 | 12 hours (pad's long press) |
| `00 00` | 0 | close |

Arbitrary values work. `08 07` (1800) gives a 30-minute open.

### Sequence

Range **1 to 15**, wrapping `15 → 1`. Zero is never used. The low nibble is
always `1` and never varies, so it appears to be a protocol marker rather than
part of the counter.

The rule is **not** that the sequence must increase. See "What the control
unit validates" below.

### Pair ID

The last four bytes are constant for a given pad/unit pairing. This is part of
how the unit recognises legitimate commands.

Whether this value is burned into the pad at manufacture or negotiated when
pad and unit are paired is **not established** — only one pairing was ever
available for testing. If you re-pair your components, re-run discovery.

### Battery byte

Byte 1 is a raw voltage proxy, **not** a percentage. It sags under transmit
load and recovers during idle, so a single reading means little. Observed
range on a healthy CR2450 was roughly `0x5C`–`0x60`, dropping to `0x48` during
sustained rapid pressing. Take a median over time if you want to use it.

The control unit's equivalent byte was constant at `0x64` in every frame
observed, so it may be a placeholder rather than a live measurement.

---

## Idle frame — pad

```
14 5E 00 "2428028669" 00 20 20 20 20
│  │  │   └────┬────┘ │  └────┬────┘
│  │  │        │      │       └─ ASCII spaces
│  │  │        │      └─ 0x00
│  │  │        └─ 10-digit serial number, ASCII
│  │  └─ always 0x00
│  └─ battery / voltage proxy
└─ frame type 0x14
```

Carries no command. Useful only for identifying the pad and reading its
battery level.

---

## State frame — control unit

```
28 64 80 "2425005661" 00 20 20 20 20
│  │  │   └────┬────┘
│  │  │        └─ 10-digit serial number, ASCII
│  │  └─ 0x80 = valve OPEN, 0x00 = valve CLOSED
│  └─ constant 0x64 in all observations
└─ frame type 0x28
```

**This is the authoritative valve state.** It is broadcast continuously, so
any listener in range always knows whether the valve is open. Never infer
state from commands you sent; read it here.

---

## What the control unit validates

Established by sending deliberately malformed and deliberately stale frames
and observing whether the valve moved:

| Property | Enforced? | Notes |
| --- | --- | --- |
| Pair ID in payload | **Yes** | must match |
| Sender BLE address | **Yes** | must be the paired pad's address |
| Sequence differs from last accepted | **Yes** | see below |
| Sequence increases | No | a stale, lower value is accepted |
| Duration value | No | any `uint16` is honoured |

### The sequence rule, precisely

A command is accepted if its sequence number **differs from the last one the
unit accepted**. It does not need to be higher.

This was established in two steps. First, sequence 3 was accepted while the
pad had most recently used 7 — so ordering is not enforced. Later, repeated
commands all using sequence 1 were ignored after the first one — so a
duplicate is rejected.

Practically: just increment your own counter and wrap at 15. If a physical
pad is also in use, track the sequence it broadcasts so you never collide with
the value it just used.

### The address requirement

This is the part that is easy to miss. A payload that is byte-for-byte
identical to a real pad command will be **silently ignored** if it comes from
a different BLE address. There is no error, no response, nothing in the air to
tell you why.

On an ESP32, set the base MAC to the pad's address minus 2 before the
Bluetooth stack starts — the chip derives its BLE address as base + 2:

```c
uint8_t base[6] = {0x7C, 0xC6, 0xB6, 0xBC, 0xF9, 0xB0};  // pad address - 2
esp_base_mac_addr_set(base);
```

Note this also changes the WiFi MAC, since both derive from the same base.

---

## The full advertisement

31 bytes, exactly the legacy advertising maximum:

```
02 01 06                      flags
05 09 4F 52 41 35             complete local name "ORA5"
15 FF 31 01                   manufacturer data header, company 0x0131
<18 payload bytes>            the command frame above
```

---

## Pad behaviour

Worth understanding, because it explains several confusing observations.

**The pad repeats its command** every ~8 ms for up to ~3.7 seconds, stopping
as soon as the control unit's state advertisement confirms the change. A
single press therefore produces several hundred identical frames. Any listener
must de-duplicate or it will drown.

**The lights mean transmit and timeout, not valve state.** Green while
broadcasting and waiting for confirmation; red when roughly two seconds pass
without it. On timeout the pad sends a cancel (close) frame and gives up.

**A red light does not mean the command failed.** The pad needs to *receive*
the unit's confirmation, and its receive sensitivity is far worse than its
transmit range. The valve can act correctly while the pad still shows red.

**The pad keeps its own open/closed model and toggles it on each press.** It
does not query the unit first. So after something else changes the valve, the
pad's next press sends the wrong direction — that press resyncs it, and a
second press does what you actually wanted.

**The pad's sequence survives sleep** and resumes across days.

---

## Timing

| Event | Observed |
| --- | --- |
| Command to state change | 80–400 ms |
| Pad repeat interval | ~8 ms |
| Pad give-up timeout | ~3.7 s |

---

## Scope and limits

Everything above comes from a single installation: one control unit, one press
pad, one firmware revision, one product variant. It has not been checked
against other Oras models or against a second pairing.

The frame layouts and the three validation rules are solid — each was
confirmed by making the valve move, not merely by reading captures. The
uncertain parts are flagged inline above, principally whether the pair ID is
per-pad or per-pairing.
