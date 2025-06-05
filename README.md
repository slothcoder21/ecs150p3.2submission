# ECS150-FS Project Part 2: Implementation Checklist

## Phase 0: Setup
- [x] Copy skeleton code (`libfs/`, `apps/`) into your repo.
- [x] Confirm `libfs/Makefile` compiles to `libfs.a` with no warnings.
- [x] Run `make clean` to verify cleanup.

## Phase 1: Mount/Unmount/Info
- [x] **fs_mount(diskname)**
  - Open disk, load superblock, FAT, root directory.
  - Validate signature and header fields.
- [x] **fs_umount()**
  - Write back superblock, FAT, root directory.
  - Close disk, reset in-memory state.
- [x] **fs_info()**
  - Print total/reserved/FAT/root/data blocks and free counts.

## Phase 2: Create/Delete/LS
- [x] **fs_create(filename)**
  - Find empty root entry, set name, size=0, FAT_EOC.
- [x] **fs_delete(filename)**
  - Locate entry, free its FAT chain, clear entry.
- [x] **fs_ls()**
  - List non-empty filenames, one per line.

## Phase 3: Open/Close/Stat/Lseek - In Progress
- [x] Maintain a table of up to 32 descriptors (root index + offset).
- [x] **fs_open(filename)**
  - Find root entry, allocate descriptor (offset=0).
- [x] **fs_close(fd)**
  - Validate and free descriptor.
- [x] **fs_stat(fd, struct stat*)**
  - Retrieve file size from root entry.
- [x] **fs_lseek(fd, offset)**
  - Validate, update descriptor’s offset.

## Phase 4: Read/Write
- [x] Helper: map (fd, offset) → data block + byte offset.
- [x] **fs_read(fd, buf, count)**
  - Read up to `count` bytes from current offset (handle partial/full blocks).
  - Update offset, return bytes read.
- [x] Helper: `allocate_new_block()`
  - Find first free FAT entry, mark FAT_EOC, zero block.
- [x] **fs_write(fd, buf, count)**
  - If writing past EOF, extend FAT chain with `allocate_new_block()`.
  - Write full/partial blocks, update file size and offset.
  - Return bytes written.

## Final Checks
- [ ] Write tests covering all operations (mount/unmount/info/create/delete/ls/open/close/stat/lseek/read/write).
- [ ] Compare outputs against `fs_ref.x`.
- [ ] Ensure `libfs/Makefile` has no errors/warnings.
- [ ] Clean repository (no binaries or temporary files).
- [ ] Package `libfs/`, `apps/`, and this `README.md` only.
