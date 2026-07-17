# Alpha.35 clean first-run package validation

Alpha.35 supersedes the withdrawn alpha.34 package. Its editor functionality is
alpha.34 plus the non-dialog automated version check.

1. Extract into a brand-new directory.
2. Confirm neither `endurance-options.json` nor `options.json` is present.
3. Start `EIMapperEndurance.exe` normally. Confirm Game and Mod Setup appears.
4. Select the game installation and JMV preset, finish setup, and confirm a new
   local `endurance-options.json` is created only in this extracted directory.
5. Restart and confirm setup does not repeat after valid settings exist.
6. Run the alpha.34 script, first-patrol, duplication, keybinding, Save As, and
   in-game checks from `ALPHA34_VALIDATION.md`.

The release staging gate must report:

`PASS: distribution contains no user settings, logs, or crash dumps`
