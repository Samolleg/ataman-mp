# EI Mapper Endurance 0.3.0-alpha.13 — persistent MPR workspace R4

This tester build keeps MPR controls attached to the loaded-map workflow and
preserves the user's landscape dock arrangement.

## Changes

- The Loaded Map MPR has a MOB-style context menu with **MPR Settings**,
  **Save MPR**, and **Save MPR As**. Save is available only for a dirty MPR.
- MPR dirty state is shown with an asterisk in Loaded Map.
- **MPR Settings** is a persistent inspector: unloading shows **No MPR loaded**
  without removing the dock, and the same inspector updates on the next load.
- Landscape Tools remembers its complete dock arrangement during the session,
  including its position relative to MPR Settings, while still restoring the
  ordinary workspace when landscape mode closes.
- Atlas Materials now explains that the displayed records are shared
  overlay/liquid parameter slots, not ground-texture choices. They remain
  read-only pending validated staged serialization.

## Safety boundary

All save entries reuse the existing staged MPR save transaction. This increment
does not add a serializer, enable material mutation, alter SEC data, or touch a
game installation.

Use `MPR_SETTINGS_HEIGHT_OPERATIONS_R1.md` cases 9–13 for the focused GUI pass.
