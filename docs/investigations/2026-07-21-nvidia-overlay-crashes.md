# Alpha.24 NVIDIA entrance-overlay crash investigation

Two private tester dumps captured the same editor failure while replacing an
MPR on NVIDIA OpenGL 4.6 (driver 591.44):

- exception `0xc0000005`, read from `0x000000000000000c`;
- NVIDIA OpenGL module offset `0x928551` in both dumps;
- Endurance return address `EIMapperEndurance.exe+0x0FCBCD` in both dumps;
- the return address is immediately after the entrance-overlay
  `glDrawArrays(GL_TRIANGLE_FAN, ...)` call;
- one run was loading `zone12.mpr`; the other was loading `zone12dun2.mpr`.

The crash was not in MOB parsing or DXT upload. The optimized mesh renderer can
leave normal, UV, or instancing attributes enabled in a mesh VAO. The entrance
overlay then releases the mesh VBO and supplies only a client-side position
array. An enabled stale normal attribute with byte offset 12 makes the driver
attempt to fetch address `0x0c`, matching the access-violation target exactly.

The fix binds the default VAO, unbinds array/index buffers, disables all vertex
attributes, and then enables only the overlay attributes it supplies. This
isolates overlay rendering from preceding mesh state. The two raw dumps remain
private and are not committed.
