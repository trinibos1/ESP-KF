# Architecture

## Tasks and Queues

Two queues isolate timing and transport:

- `eventQueue` (`KeyEvent`): produced by `matrix_task`, consumed by `core_task`
- `reportQueue` (`HidReport`): produced by `core_task`, consumed by `router_task`

Tasks:
- `matrix_task`: rate-limited scan (default 1kHz max) + debounce → `eventQueue`
- `core_task`: strict pipeline → dirty report snapshots → `reportQueue`
- `router_task`: transport state machine + distribution to per-transport mailboxes
- `usb_task`: sends keyboard reports at a fixed poll interval (default 1ms) when USB is active
- `ble_task`: sends keyboard reports over BLE GATT notifications when BLE is active
- `macro_task`: scaffold (non-blocking macro engine TODO)

## Strict Event Pipeline (core_task)
Order is fixed:
1. preprocess
2. tap/hold (policy selectable)
3. combos (combo priority rule)
4. layer resolve (`KC_TRANSPARENT` fall-through)
5. keycode → action(s)
6. report builder (6KRO)
7. enqueue (latest-state-wins)

## Transport
Router owns `g_espkm_transport_state`:
- `NONE` (0): No active connections
- `USB_ACTIVE` (1): Only USB is connected
- `BLE_ACTIVE` (2): Only BLE is connected
- `BOTH_ACTIVE` (3): Both USB and BLE are connected (dual-transport mode)

Both USB and BLE send reports simultaneously when available, enabling dual-host support.

