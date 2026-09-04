# mausJiggler

USB HID mouse jiggler firmware for the **STM32F103C8** (“Blue Pill”). The board enumerates as a standard USB mouse and periodically moves the cursor along smooth, random elliptical paths so the host PC does not go idle.

Built with **STM32CubeMX** / **STM32CubeIDE**, using the ST USB Device Library (HID mouse class).

---

## Features

- Presents as a **USB HID mouse** (no drivers on Windows / Linux / macOS)
- **Smooth curved movement** along random elliptical arcs
- **Random timing** between moves (3–8 s) and random path geometry
- **Onboard LED** status: USB link health and activity
- Tunable movement speed, smoothness, and idle intervals via constants in `Src/main.c`

---

## Hardware

| Item | Detail |
|------|--------|
| MCU | STM32F103C8T6 (64 KB flash, 20 KB RAM) |
| Board | STM32 “Blue Pill” (or compatible) |
| Crystal | **8 MHz HSE** (marking `8.000`) — default firmware config |
| USB | Micro-USB on **PA11** (D−) / **PA12** (D+) |
| LED | **PC13** (active-low on most Blue Pill clones) |
| Debug | SWD on PA13/PA14 (optional; disconnect for normal use) |

### Wiring notes

- **R10** (1.5 kΩ on D+) is required for USB enumeration — standard on Blue Pill.
- Use a **data-capable** USB cable (not charge-only).
- For daily use: power from **micro-USB only**; unplug **ST-Link USB** and SWD so the device port is free.

### Windows identity

| Field | Value |
|-------|--------|
| VID | `1A2C` |
| PID | `0003` |
| Manufacturer | `Generic` |
| Product | `USB Optical Mouse` |

Device Manager typically shows **USB Optical Mouse** or **HID-compliant mouse** under Mice and pointing devices — not STM32/STMicro.

```powershell
Get-PnpDevice | Where-Object { $_.InstanceId -like '*VID_1A2C&PID_0003*' }
```

**What admins can still see:** VID/PID in hardware IDs, HID mouse class, and a hex serial from the chip UID. They will **not** see STMicro strings or ST VID `0483` unless CubeMX regenerates `usbd_desc.c` and overwrites these values.

---

## Quick start

1. Open the project in **STM32CubeIDE** (`mouse_jiggler.ioc`).
2. Build the **Debug** configuration.
3. Flash with ST-Link (SWD).
4. Disconnect ST-Link; plug **only** the Blue Pill micro-USB into the PC.
5. Wait ~3 s startup, then ~3–8 s until the first cursor movement.

Verify on Windows:

```powershell
Get-PnpDevice | Where-Object { $_.InstanceId -like '*PID_0003*' -and $_.InstanceId -like '*VID_1A2C*' }
```

---

## LED behaviour

| Pattern | Meaning |
|---------|---------|
| **Fast continuous blink** | No USB setup packets from host (cable, port, or jack issue) |
| **N blinks, pause, repeat** | `N` = USB `dev_state` (1 = DEFAULT, 2 = ADDRESSED, 4 = SUSPENDED) |
| **Slow blink (~1 s)** | USB configured; startup or waiting between moves |
| **Faster blink (~0.5 s)** | Actively sending mouse movement |
| **Very fast strobe (~50 ms)** | `Error_Handler` — usually HSE crystal failure |

---

## Tuning movement

Edit the constants at the top of `Src/main.c`:

```c
#define JIGGLE_WAIT_MIN_MS      3000   // min idle time between moves
#define JIGGLE_WAIT_RANGE_MS    5000   // + random → 3–8 s total
#define JIGGLE_STEP_MIN_MS      16     // HID report interval (min)
#define JIGGLE_STEP_RANGE_MS      8    // → 16–24 ms between reports
#define JIGGLE_MOVE_MIN_MS       900   // duration of one arc (min)
#define JIGGLE_MOVE_RANGE_MS     600    // → 0.9–1.5 s per move
#define JIGGLE_MAX_DELTA           2    // max pixels per HID report
```

| Goal | Suggestion |
|------|------------|
| Slower arcs | Increase `JIGGLE_MOVE_MIN_MS` / `JIGGLE_MOVE_RANGE_MS` |
| Smoother motion | Lower `JIGGLE_MAX_DELTA` to `1`, shorten step interval |
| Less frequent jiggles | Increase `JIGGLE_WAIT_*` |
| Larger/smaller moves | Adjust `randf()` ranges in `start_new_move()` |

### Clock options

```c
#define USE_HSI_CLOCK  0          // 0 = HSE (required for correct USB)
#define BOARD_HSE_MHZ  8          // 8 or 12 to match crystal marking
```

USB on STM32F103 needs **72 MHz SYSCLK** and **48 MHz USB clock** from HSE. HSI cannot provide a spec-compliant USB clock.

---

## Project structure

```
mausJiggler/
├── Src/
│   ├── main.c              # App state machine, jiggler math, LED, clock
│   ├── usb_device.c        # USB init, USB_IsConfigured(), helpers
│   ├── usbd_conf.c         # Low-level USB (PCD), IRQ callbacks, PMA
│   ├── usbd_desc.c         # USB descriptors (VID/PID, strings)
│   ├── stm32f1xx_it.c      # USB_LP_CAN1_RX0_IRQHandler
│   └── ...
├── Inc/
│   ├── usb_device.h
│   ├── usbd_conf.h
│   └── ...
├── Middlewares/ST/STM32_USB_Device_Library/
│   ├── Core/               # USB device core, control requests
│   └── Class/HID/          # HID mouse class, report descriptor
├── Startup/
├── mouse_jiggler.ioc       # CubeMX configuration
└── STM32F103C8TX_FLASH.ld  # Linker (heap 0x400, stack 0x800)
```

### Key source files (application logic)

| File | Role |
|------|------|
| `main.c` | `app_poll()`, jiggler, LED, `SystemClock_Config()` |
| `usb_device.c` | `MX_USB_DEVICE_Init()`, connection helpers |
| `usbd_conf.c` | `HAL_PCD_*` callbacks, endpoint PMA, static HID heap |
| `usbd_desc.c` | Device/config/string descriptors |
| `usbd_hid.c` | HID class, report descriptor, `USBD_HID_SendReport()` |

---

## Code flow overview

High-level picture from power-on to cursor movement:

```mermaid
flowchart TB
    subgraph Boot["Power-on & init"]
        A[Reset] --> B[HAL_Init]
        B --> C[SystemClock_Config<br/>HSE 8MHz ×9 → 72MHz<br/>USB 48MHz]
        C --> D[MX_GPIO_Init<br/>PC13 LED]
        D --> E[MX_USB_DEVICE_Init<br/>HID class + USBD_Start]
        E --> F[srand + led_off]
    end

    subgraph Loop["while 1"]
        F --> G[app_poll]
        G --> G
    end

    subgraph USB_IRQ["Parallel: USB interrupt"]
        H[USB_LP_CAN1_RX0_IRQHandler] --> I[HAL_PCD_IRQHandler]
        I --> J[Setup / data / reset callbacks]
        J --> K[USBD core + HID class]
    end

    E -.-> H
    K -.-> G
```

---

## Main loop (`app_poll`)

Every iteration of `main()` calls `app_poll()`. USB work runs in interrupts; the main thread only drives the jiggler and LED.

```mermaid
flowchart TD
    Start([app_poll]) --> USB{USB_IsConfigured?<br/>CONFIGURED or SUSPENDED}
    USB -->|No| LED_DBG[led_usb_not_configured<br/>blink pattern by dev_state]
    LED_DBG --> End([return])

    USB -->|Yes| SW{app_state}

    SW -->|APP_STARTUP| ST[LED slow blink<br/>wait STARTUP_DELAY_MS 3s]
    ST --> ST2{elapsed?}
    ST2 -->|No| End
    ST2 -->|Yes| WAIT[app_state = APP_WAITING<br/>schedule next_action_tick]

    SW -->|APP_WAITING| WT[LED slow blink]
    WT --> WT2{now >= next_action_tick?}
    WT2 -->|No| End
    WT2 -->|Yes| MV[start_new_move<br/>app_state = APP_MOVING]

    SW -->|APP_MOVING| MV2[LED faster blink]
    MV2 --> JS[jiggler_step]
    JS --> JS2{jiggle_state == WAITING?}
    JS2 -->|Yes| BACK[app_state = APP_WAITING<br/>schedule next move 3–8s]
    JS2 -->|No| End
    BACK --> End
    WAIT --> End
    MV --> End
```

### Application state machine

```mermaid
stateDiagram-v2
    [*] --> APP_STARTUP: power-on
    APP_STARTUP --> APP_WAITING: 3 s elapsed, USB OK
    APP_WAITING --> APP_MOVING: next_action_tick
    APP_MOVING --> APP_WAITING: move complete
    APP_WAITING --> APP_WAITING: idle 3–8 s

    note right of APP_STARTUP
        LED: ~1 s period
        No mouse reports
    end note

    note right of APP_MOVING
        LED: ~0.5 s period
        jiggler_step each loop
    end note
```

---

## Jiggler movement

Each move is a **time-based** chase along a random elliptical arc (not fixed waypoint jumps). Progress `move_t` goes from 0 → 1 over `move_duration_ms` (~0.9–1.5 s).

```mermaid
flowchart TD
    SN[start_new_move] --> R[Pick random ellipse_a, ellipse_b,<br/>rotation, start_angle, sweep_angle]
    R --> T[sent_x/y = 0<br/>move_start_tick = now<br/>move_duration_ms random]
    T --> MOV[jiggle_state = MOVING]

    MOV --> STEP[jiggler_step]

    STEP --> INT{step_interval elapsed?}
    INT -->|No| DONE([return])
    INT -->|Yes| MT[move_t = elapsed / duration<br/>clamp to 1.0]

    MT --> EASE[eased = cosine ease<br/>angle = start + sweep × eased]
    EASE --> EP[ellipse_point → target x,y]

    EP --> FIN{move_t ≥ 1 AND<br/>within 2 px of target?}
    FIN -->|Yes| WAIT[jiggle_state = WAITING<br/>schedule next_action_tick]
    WAIT --> DONE

    FIN -->|No| DELTA[dx/dy = target − sent<br/>clamp to ±JIGGLE_MAX_DELTA]
    DELTA --> HID{send_hid_move?}
    HID -->|No endpoint idle| DONE
    HID -->|OK| UPD[sent_x/y += report<br/>last_step_tick = now]
    UPD --> DONE
```

### Ellipse geometry

```mermaid
flowchart LR
    subgraph Math["ellipse_point(angle)"]
        P1["ex = a × cos(angle)<br/>ey = b × sin(angle)"] --> P2["Rotate by rotation θ"]
        P2 --> P3["(x, y) target on path"]
    end

    subgraph HID["HID report (4 bytes)"]
        B0["[0] buttons = 0"]
        B1["[1] dx int8"]
        B2["[2] dy int8"]
        B3["[3] wheel = 0"]
    end

    P3 --> DELTA["Δ = target − sent"]
    DELTA --> HID
```

Movement is **relative**: each report sends small `dx`/`dy` deltas; the host accumulates them. `sent_x`/`sent_y` track how far we have sent along the current arc (not absolute screen position).

---

## HID send path

```mermaid
sequenceDiagram
    participant Main as main loop<br/>jiggler_step
    participant Send as send_hid_move
    participant HID as USBD_HID_SendReport
    participant IRQ as USB interrupt
    participant Host as PC

    Main->>Send: dx, dy
    Send->>Send: hid_endpoint_ready?<br/>CONFIGURED/SUSPENDED + HID_IDLE
    alt not ready
        Send-->>Main: fail (retry later)
    else ready
        Send->>HID: hid_report[4]
        HID->>Host: IN token EP 0x81
        HID-->>Send: state = HID_BUSY
        Send-->>Main: success
        Host->>IRQ: transfer complete
        IRQ->>HID: state = HID_IDLE
    end
```

`send_hid_move()` only fires when the previous report finished (`HID_IDLE`). That prevents overrunning the single interrupt IN endpoint.

---

## USB stack flow

CubeMX generates a standard **USB FS device + HID mouse** stack.

```mermaid
flowchart TB
    subgraph Host["USB host (PC)"]
        H1[Detect D+ pull-up]
        H2[Reset + SET_ADDRESS]
        H3[GET_DESCRIPTOR]
        H4[SET_CONFIGURATION]
        H5[Poll HID IN endpoint]
    end

    subgraph MCU["STM32F103"]
        subgraph HW["Hardware"]
            PA11[PA11 USB_DM]
            PA12[PA12 USB_DP + R10 1.5k]
        end

        subgraph IRQ["usbd_conf.c / HAL PCD"]
            CB[HAL_PCD_ResetCallback<br/>HAL_PCD_SetupStageCallback<br/>HAL_PCD_DataInCallback ...]
        end

        subgraph Stack["ST USB Device Library"]
            CORE[usbd_core.c<br/>usbd_ctlreq.c]
            DESC[usbd_desc.c]
            CLASS[usbd_hid.c]
        end

        APP[main.c app_poll]
    end

    H1 --> PA12
    H2 --> CB --> CORE
    H3 --> DESC
    H4 --> CLASS
    H5 --> CLASS
    CLASS --> APP
```

### Enumeration sequence

```mermaid
sequenceDiagram
    participant Host
    participant EP0 as Control EP0
    participant Dev as Firmware dev_state

    Host->>EP0: USB RESET
    Note over Dev: DEFAULT (1)
    Host->>EP0: GET_DESCRIPTOR (device)
    Host->>EP0: SET_ADDRESS
    Note over Dev: ADDRESSED (2)
    Host->>EP0: GET_DESCRIPTOR (config, strings, HID report)
    Host->>EP0: SET_CONFIGURATION
    Note over Dev: CONFIGURED (3)
    Host->>EP0: SET_IDLE / optional class requests
    loop Every 16–24 ms while moving
        Host->>Dev: IN poll EP 0x81
        Dev-->>Host: HID mouse report
    end
```

### Custom USB fixes in this project

Several changes were made on top of the CubeMX defaults for reliable Blue Pill enumeration:

| Area | Change |
|------|--------|
| `usbd_ctlreq.c` | Set `CONFIGURED` only **after** `USBD_SetClassConfig()` succeeds |
| `usbd_hid.c` | Allow `USBD_HID_SendReport()` in **SUSPENDED** state |
| `usbd_conf.c` | Ignore suspend until configured; reset HID alloc on bus reset |
| `usbd_desc.c` | Fallback serial string if chip UID is zero |
| `main.c` | HSE required (no silent HSI fallback); verify `HSERDY` |

---

## Building

### Requirements

- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) 1.13+ (or compatible GCC ARM toolchain)
- ST-Link V2 (or clone) for programming
- Optional: `STM32CubeProgrammer` for standalone flashing

### Steps

1. **File → Import → Existing Projects into Workspace** → select repo root.
2. Select project **mouse_jiggler**.
3. **Project → Build Project** (Debug configuration).
4. **Run → Debug** (or flash `Debug/mouse_jiggler.hex`).

Linker settings (`STM32F103C8TX_FLASH.ld`):

- Flash: 64 KB
- RAM: 20 KB
- Heap: `0x400`, Stack: `0x800`

---

## Troubleshooting

| Symptom | Things to check |
|---------|------------------|
| No device in Device Manager | Data cable; USB jack solder; ST-Link disconnected; try rear USB 2.0 port |
| LED fast blink forever | Host not seeing USB data lines |
| LED 2 blinks, no enumeration | HSE/crystal; wrong `BOARD_HSE_MHZ`; charge-only cable |
| LED very fast strobe | HSE failed — check 8 MHz crystal and load caps |
| Mouse moved once then stopped | Fixed in current firmware (SUSPENDED + time-based path) — reflash latest |
| Cursor too fast/choppy | Adjust `JIGGLE_*` constants in `main.c` |

### Debug variables (SWD)

| Symbol | Expected when working |
|--------|------------------------|
| `hUsbDeviceFS.dev_state` | `3` (CONFIGURED) or `4` (SUSPENDED) |
| `g_usb_setup_count` | Increases on plug-in |
| `g_usb_config_ok_count` | ≥ 1 after enumeration |

---

## License

STM32 HAL and USB middleware: **ST SLA0044** (see ST distribution files).  
Application code in `Src/main.c` and project-specific changes: use per your repository license.

---

## Acknowledgements

- [STM32CubeMX](https://www.st.com/stm32cubemx) / STM32CubeF1 HAL
- ST USB Device Library (HID mouse template)
