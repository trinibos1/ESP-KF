# ESP32-S3 USB HID Fixes / Changes

After spending an amount of time I'd rather not say debugging USB HID on the ESP32-S3, I ended up using AI to help track down the issues. Here is a full breakdown of everything that was changed, tested, and identified during the debugging process.
Have fun reading :> .
---

## 1. Disabled USB Serial/JTAG so TinyUSB could use the USB PHY

### Problem
USB Serial/JTAG was taking ownership of the ESP32-S3 internal USB PHY, preventing TinyUSB HID from enumerating correctly.

### Changes in `sdkconfig.defaults`

#### Added
```ini
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
# CONFIG_USJ_ENABLE_USB_SERIAL_JTAG is not set
```

#### Already disabled / confirmed
```ini
# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG is not set
# CONFIG_ESP_CONSOLE_USB_CDC is not set
```

### Why
ESP32-S3 cannot use:
- USB Serial/JTAG
- TinyUSB OTG HID

at the same time because they share:
- GPIO19
- GPIO20
- same internal USB PHY

---

## 2. Regenerated `sdkconfig` from defaults

### Why
Old `sdkconfig` values were overriding new settings in `sdkconfig.defaults`.

---

## 3. Fixed HID Report ID mismatch

### File
`usb.c`

### Before
```c
bool ok = tud_hid_keyboard_report(0, r.modifiers, r.keys);
```

### After
```c
bool ok = tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, r.modifiers, r.keys);
```

### Why
Descriptor declared:

```c
HID_REPORT_ID(1)
```

but reports were being sent with:
- report ID `0`

Windows enumerated the HID device but silently discarded all keypresses.

---

## 4. Confirmed HID enumeration works

### Result
After disabling USB Serial/JTAG:
- HID device appeared in Windows
- COM port disappeared during runtime (expected behavior)

### Why
TinyUSB successfully took ownership of the USB PHY.

---

## 5. Clean rebuild + flash process

### Why
Ensured:
- no stale config
- no stale object files
- full rebuild with corrected USB config

---

## 6. Identified possible TinyUSB task conflict

### Problem
`usb_task()` was manually calling:

```c
tud_task();
```

while `esp_tinyusb` may already create its own TinyUSB task internally.

This can cause:
- dropped reports
- corrupted USB state
- HID device enumerates but no typing

### Suggested change in `usb.c`

#### Removed
```c
#ifndef CONFIG_TINYUSB_TASK_ENABLE
tud_task();
vTaskDelay(pdMS_TO_TICKS(1));
#endif
```

#### Replaced with
```c
for (;;) {
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONFIG_ESPKM_USB_POLL_MS));
```

### Why
TinyUSB is not thread-safe.  
Two tasks calling `tud_task()` can break HID transmission.

---

## 7. Confirmed BLE pipeline works

### Confirmed working
- Matrix scanning
- Key processing
- Router
- BLE HID output

### Meaning
The issue was isolated specifically to the USB HID path.

---

## 8. Confirmed router transport logic

### Verified state machine
```c
ESPKM_TRANSPORT_USB_ACTIVE
ESPKM_TRANSPORT_BOTH_ACTIVE
```

was implemented correctly inside `router.c`.

---

## 9. USB HID now enumerates correctly

### Final state reached
- USB HID device appears
- BLE still functions
- USB PHY conflict resolved
- HID report ID fixed

### Remaining suspected issue
- TinyUSB task conflict
- USB send timing issue

---

# Files Modified

## Config
- `sdkconfig.defaults`

## Source
- `usb.c`

## Regenerated
- `sdkconfig`

---

# Important Behavior Changes

## USB Serial/JTAG disabled

This means:
- COM port disappears during runtime
- HID owns USB instead

## For logs/debugging now

Need:
- UART adapter on GPIO43/GPIO44
- OR temporary USB CDC mode

---

# Final Likely Working USB Stack Setup

## TinyUSB owns PHY
```ini
# CONFIG_USJ_ENABLE_USB_SERIAL_JTAG is not set
```

## Correct HID report ID
```c
tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, ...)
```

## Only one TinyUSB task
- remove duplicate `tud_task()` calls