# EI Mapper: Endurance Edition

EI Mapper: Endurance Edition is an independently versioned derivative of
[konstvest/ei_maper](https://github.com/konstvest/ei_maper), an Evil Islands map
editor originally authored by konstvest and contributors.

The Endurance Edition work starts from upstream commit `1cebe83d` and retains
the upstream Git history. It is distributed under GNU GPL-3.0, the same license
as the upstream project.

Endurance Edition changes focus on lossless map handling, explicit validation,
resource compatibility, performance, and Jamais Vu modding workflows. The
Endurance name and version line do not imply endorsement by the upstream
maintainer.

The embedded MPR map-name handling incorporates the concept proposed by
Grabarrz90 in upstream pull request #19, with additional Endurance validation
and indexed resource-resolution changes.

Landscape-atlas format research also consulted aspadm's
[EI-HD-tiles](https://github.com/aspadm/EI-HD-tiles) documentation and map
descriptions. The compact Zone1 slot-topology compatibility table used by
semantic painting was derived from that project's public Zone1 map description.
Endurance does not include its tools, generated atlases, YAML descriptions,
source code, or texture assets. EI-HD-tiles was developed by aspadm and is
distributed for non-commercial use with attribution; it is acknowledged here
as an independent research source.

Character FIG/UV pipeline research was cross-checked against
[konstvest/ei_figer](https://github.com/konstvest/ei_figer), the GPL-3.0 Blender
add-on for importing and exporting Evil Islands models. No game or mod assets
are redistributed with Endurance.
