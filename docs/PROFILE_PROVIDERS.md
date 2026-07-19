# Resource-profile providers

Endurance 0.3 uses provider bindings as the boundary between a resource
profile and game/mod semantics. A profile ID locates resources and appears in
the UI; it does not select database, appearance, equipment, animation, or
validation behavior. Those roles are selected by the profile's `providers`
manifest object.

The built-in provider IDs are:

- `base.standard.v1`: Base Evil Islands rules.
- `jmv.v1`: Jamais Vu rules and the reference implementation for mod authors.

Both are registered source providers. Dynamic third-party DLL discovery, a
stable binary ABI, signing/trust, process isolation, and crash containment are
explicitly deferred. A future mod can already reuse either provider with new
resource roots, or add a source provider and register a unique versioned ID.
Feature code must not branch on a literal profile ID.

## Roles and failure behavior

Schema version 2 supports `database`, `characterAppearance`,
`equipmentLayout`, `unitAnimation`, and `semanticValidation`. A binding may be
a provider ID string or an object with `id` and `interfaceVersion`. Unknown
IDs, duplicate registrations, role mismatches, and incompatible interface
versions fail closed with diagnostics. Endurance does not silently substitute
Base or JMV behavior.

Provider capability declarations belong beside the implementation. JMV-only
equipment and animation masks must remain in the JMV module. The existing
hardcoded JMV map-launch bridge predates this boundary and remains separate
legacy migration work.

## Immutable database snapshots

`ProfileDatabaseService` publishes immutable snapshots through one atomic
`ProfileDatabaseContext` pointer swap. Cache identity includes provider ID and
interface version, profile ID, runtime manifest, ordered source paths, and the
SHA-256 of the selected database archives. Snapshots contain stable-name links,
current dropdown names, action-table bytes, structured diagnostics, and member
provenance. They are read-only inputs; a future datatable editor must use a
separate transaction/export layer.

Base selects the first active archive named exactly `database.res` from its
ordered resource roots. JMV selects `database.res` only from its highest
priority mod root for current relational tables, then overlays only ADB action
members from the sibling `databaseJmv.res`. It never obtains missing JMV
relational rows from Base. Archives whose names indicate backup, original,
unmodified, or old content are not candidates.

JMV visual resolution owns its corpus-specific exceptions. Numeric human and
orc skin values select numbered face resources. Database appearance/equipment
substitution is limited to known wearable humanoid assemblies. Creature actors
retain their MOB-resolved model, texture, component mask, and integral parts;
their numeric database skin values are not RaceModels-list indices.
Equipment textures are derived from item slot/type, TTI/TTI2, and material
code, then bound per assembly part. Base and future providers do not inherit
these rules implicitly.

## Adding a mod provider

1. Choose a stable, namespaced, versioned provider ID.
2. Declare the roles and interface version it implements.
3. Put provider-specific resource precedence and gates in one provider module.
4. Bind the ID in a schema-version-2 resource-profile manifest.
5. Add tests for manifest selection, unknown-provider failure, Base/JMV
   isolation, cache/provenance identity, diagnostics, and every special layout.
6. Keep MOB and database inputs immutable and produce transient render plans.

See `resource-profiles/jamais-vu.json.example` for the reference manifest.
The animation role and runtime validation matrix are documented in
`docs/UNIT_IDLE_ANIMATION.md`.
