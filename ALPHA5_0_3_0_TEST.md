# EI Mapper Endurance 0.3.0-alpha.5

Focused test candidate for collections without a `map.txt` identity.

- Fixes the guarded runtime layout used to borrow and restore one process-local
  bootstrap zone record. Alpha.4 found the record but rejected its MFC exit
  array because the array object header was omitted from the offsets.
- Clears old entrance rectangles immediately when a newly loaded collection or
  MPR has no map identity.
- Does not modify installed `map.txt`, source maps, or live saves.

Please test both a fresh Test Current Map from the map-less empty control and a
hot reload from a normal mapped zone into that collection using Play from Here.
