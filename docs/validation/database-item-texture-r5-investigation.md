# Database item texture: failed r4 gameplay acceptance

Status: root-cause investigation; no replacement build or gameplay pass.

## Original repeatable case

Place `cheat dagger` with bronze material on zone1 as an ordinary object, save,
then load that map in JMV. Expected: the same textured dagger in the editor and
game, including after conversion to a lever. Actual: the user's new screenshot
shows both the manually corrected old dagger and the newly placed r4 dagger white.

Read-only map inspection confirms both records (334 and 335) use `unhuma`, select
only `rh3.dagger03`, and have the same primary and secondary identifier:
`unhumadg_03.br.1.x2`. Neither contains `.mmp`. The old object is elongated;
the new object has the default complexion. That difference does not explain the
shared texture failure. The earlier extension normalization was necessary but
did not resolve the reported gameplay issue.

## First incorrect representation

The picker accepts a single equipment component texture as a ready ordinary
object texture if the editor can render it. These are different contracts.

The selected archive member is a 512 x 512 PNT3 equipment layer, with compressed
payload length 148756 bytes. The ordinary game texture path requests a directly
supported surface format; PNT3 is a layer-composition format, not such a surface.
Finding the exact resource therefore does not establish that it can be bound
directly. The base bronze variant is also PNT3, so dropping `.x2` is not a fix.

There is also a layout dependency. The JMV part's authored UV bounds are
approximately U [0.027784, 0.138039], V [0.516876, 0.981538]. The editor's
transient equipment path applies the x2 repeat transform. Saving the layer name
does not persist that transform for an ordinary game object. Format conversion
alone must not be accepted without checking the final atlas layout.

## Concrete next change and acceptance

Build a ready map texture from the selected material layer, including the atlas
layout needed by the unchanged model, and encode a directly loadable MMP surface.
Use a new collision-resistant resource identity; preserve installed originals.
The placed object and its lever conversion must reference that ready texture.
The texture must be delivered with the map as a separate resource archive.
Existing resource codecs provide PNT3 decoding and ordinary 8888 MMP encoding.

Before exporting actual installed material data, obtain the project-required
permission to create an edited working copy. Proposed local-only destination:
ignored `build-artifacts/database-item-texture-r5/`; no source assets in Git or
the editor distribution. Installing that resource into a live mod is a separate
write and is not authorized by this investigation.

Regression checks must verify encoded format/header, exact stored identity,
atlas sampling without preview-only adjustments, undo/save/reopen and lever
conversion. Final acceptance still requires an in-game frame of the original
case. No game or map files were modified, and no installed assets were copied.
