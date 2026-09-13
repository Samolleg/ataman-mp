# EI-MOB-SCENARIO-VISUAL-001-A — connected MOB follow-up

Parent: [first-stage report](MOB_SCENARIO_R1.md). Local baseline:
`57a9589e7746e47dc8ba6d3e767339289f052ac6`, same contributor branch and authority.

User symptom: connected MOBs appear not to participate in the scenario layer.
The precise missing participant is not yet identified. Read-only inspection of
the tester workflow confirmed JMV zone1 and zone3obr loading and family reports;
no user settings, logs, installed maps or other worktrees were modified.

Two repeatable faults were captured before their respective implementation fixes:

1. `dependencyNamesWithoutExtension`: AddMob("QUEST") must include quest.mob and
   its nested dependency, resolve the nested object's ID, and produce its movement
   marker. Before: familyComplete false, 1 failed test. First incorrect decision:
   filename lookup lacked the family loader's optional-extension normalization.
2. Hidden native panel fixture: an active supplemental MOB has an inspected,
   unopened owning primary and loaded/unloaded chained dependencies. Expected:
   4 family sources, 16 visible references and 5 markers; removing AddMob in the
   draft prunes the subtree. Before: 4 integration failures. First incorrect
   decision: panel discarded the owning primary because it was not open.

The fixes normalize only the optional `.mob` suffix and retain an inspected
primary when the active document belongs to its reported family. They do not
infer unknown sources or include arbitrary neighboring maps. Regressions cover
case-insensitive names, recursive closure, closing the connected editor document,
unapplied dependency removal and stale report isolation on unrelated map switch.

Validation on the final binary:

- Release wrapper including runtime components passed.
- Scenario model: 15 passed, 0 failed (previous 14 plus original name regression).
- Hidden Windows Desktop OpenGL integration: 38 passed, 0 failed, including the
  original R1 checks and 5 connected-family checks.
- Hidden Windows ANGLE with read-only JMV zone3obr corpus: 42 passed, 0 failed.
  Aggregate: 9 inspected sources, 2191 references, 978 conditioned records,
  1 spawn call, 295 unresolved targets, 26 markers, familyComplete true (39 ms).
  The input file hash remains unchanged. This aggregate is not interactive proof.
- Full UI diagnostic remains the known R1 evidence gap; not rerun for these
  analysis/root changes. No claim of full UI regression coverage.

Executable SHA-256:
`2725befff70447302ece1b0c7f78af093e637749fa786f88889dd06579ca10fd`.

Handoff audit: the two defects and fixes are proven by focused failing/passing
tests. Their relationship to the exact user's observation is still unconfirmed.
Unloaded placed-object model rendering and arbitrary symbolic dependency
evaluation remain outside the implemented behavior, as in R1.

Delivered: `C:/GOG Games/maped/EI-Mapper-Endurance-0.5.0-alpha-mob-scenario-r2-test`.
Staging and destination finalizers passed; all 92 files have identical hashes.
R1's user settings/logs and executable were preserved for comparison. No packaged
executable was launched. Next: test the exact missing participant with the user;
keep the parent and follow-up open for acceptance.
