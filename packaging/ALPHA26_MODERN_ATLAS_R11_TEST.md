# Modern atlas r11 tester notes

This build adds five editor-openable Zone18 expanded-atlas collections covering
1,024, 2,048, 4,096, 8,192, and 16,384 terrain slots. Each collection keeps its
generated isolated MPR beside the matching `-atlas.res` and resolves the stock
`zone18.mob` from the active Jamais Vu Maps directory. Installed resources are
not copied into or modified by the test package.

The editor's **Test Current Map** staging path rebases the selected MPR and all
adjacent atlas pages to a random run-owned namespace, stages the default Zone18
MOB, and removes owned live aliases after the test. A separate automated
campaign forced `drawall`, recorded memory/address-space telemetry, and passed
through the packed-format maximum of 16,384 slots.

## Focused manual test

1. Select the `jamais-vu` resource profile and refresh Maps and Collections.
2. Open `Zone18 expanded atlas - 1024 slots` from Collections. Confirm the MPR
   and default `zone18.mob` load and terrain is textured.
3. Use **Test Current Map** and confirm Zone18 reaches gameplay without missing
   terrain. Stop the test normally.
4. Repeat in order for 2,048, 4,096, 8,192, and 16,384 slots. Allow the largest
   collections time to load; do not treat normal paging as a hang.
5. After each run, confirm no randomly named `ed........` MPR/MOB or
   `EIMapperEndurance-*.res` remains in the JMV installation.

## Expected scope and limitations

- The expanded MPRs are deliberate capacity probes made from installed JMV
  Zone18 imagery. Their repeated/checkered layout is not an artistic blend test.
- The default Zone18 MOB is resolved from the configured JMV installation and
  is not redistributed in this package.
- Existing expanded packages can be staged through **Test Current Map**, but
  normal editor atlas finalization remains conservatively limited to 1,024
  slots pending a separate authoring-policy change.
- Runtime validation proves all expected pages resolve; it does not claim all
  pages remain simultaneously resident on the GPU.
