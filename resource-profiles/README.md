# Endurance resource profiles

Endurance discovers JSON profile files from a `resource-profiles` directory
beside `EIMapperEndurance.exe`. Copy `jamais-vu.json.example`, give it a unique
`id` and `displayName`, then adjust its relative paths and signatures for your
mod. Rename the finished file to end in `.json` so it appears in **Game and Mod
Setup**.

Paths use `/` in the JSON and support these variables:

- `{gameRoot}`: the Evil Islands installation selected by the user.
- `{modRoot}`: the detected mod directory, when the profile defines mod
  discovery rules.

`resourceDirectories` are ordered from highest to lowest priority. Endurance
indexes every `.res` archive directly inside each directory. A profile never
needs to list individual archive filenames.

Profile schema version 2 adds manifest-selected providers. A profile may reuse
the built-in `base.standard.v1` or `jmv.v1` provider with different resource
roots; selection is independent of the profile ID. The required provider roles
are `database`, `characterAppearance`, `equipmentLayout`, `unitAnimation`, and
`semanticValidation`. Unknown IDs and incompatible interface versions fail
closed with diagnostics rather than silently using Base or JMV behavior.

Profile fields:

- `id`, `displayName`, `resourceDirectories`: required.
- `mapDirectories`: optional map locations retained by the workspace profile.
- `fixedModRoots`: likely mod locations relative to the game root.
- `searchParent`: directory whose immediate children are inspected as fallback
  mod roots.
- `directoryNameContains`: terms used to prefer a matching directory.
- `requiredFiles`, `requiredDirectories`: candidate validation rules.
- `signatureFiles`: strong preference signals when several candidates match.
- `providers`: role-to-provider bindings (schema version 2). A value may be a
  provider ID string or `{ "id": "...", "interfaceVersion": 1 }`.

Invalid JSON profiles are ignored. The selected configuration is previewed and
must validate before it can be applied.
