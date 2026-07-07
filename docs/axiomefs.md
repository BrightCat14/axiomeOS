# axiomefs Specification v1

## Key Principles

* 64-bit file system.
* Copy-on-Write for all metadata.
* Atomic transactions.
* Snapshot support.
* Support for multiple volumes within a single container.
* B+Tree for most structures.
* UTF-8 filenames.
* Little-endian.

---

# Disk Layout

```
+---------------------+
| Superblock A        |
+---------------------+
| Superblock B        |
+---------------------+
| Object Map          |
+---------------------+
| Checkpoint Area     |
+---------------------+
| Metadata            |
+---------------------+
| Data Blocks         |
+---------------------+
```

Two superblocks are required for crash recovery.

---

# Block Size

```
4096 bytes (default)

Supported sizes:
4096
8192
16384
```

All structures are multiples of the block size.

---

# Object ID

Each object is assigned a unique identifier.

```c
typedef uint64_t object_id;
```

Used for:

* inode
* btree
* extent
* snapshot
* directory
* xattr

---

# Object Header

Every object begins with the same header.

```c
struct object_header {
u64 object_id; 
u64 transaction_id; 
u32 type; 
u32 flags; 
u64 checksum;
};
```

This allows any object to be read in the same way.

---

# Superblock

```c
struct superblock {

object_header hdr; 

char magic[8]; 

u64 block_size; 

u64 total_blocks; 

u64 root_tree; 

u64 object_map; 

u64 checkpoint_tree; 

u64 transaction_id;

};
```

---

# Inode

```c
struct inode {

object_header hdr; 

u64 inode_number; 

u64 size; 

u64 created; 

u64 modified; 

u64 permissions; 

u32 uid; 

u32 gid; 

u64 extent_tree; 

u64 xattr_tree;

};
```

The file does not store blocks directly.

It stores an extent tree.

---

# Extents

Extents are used instead of a list of blocks.

```text
File

↓

Extent Tree

↓

Extent

↓

start block
length
```

For example

```
Block 900

Length 128
```

means

```
900..1027
```

---

# Directory

A directory is a B+Tree.

Key

```
file name
```

Value

```
inode_id
```

Search complexity is

```
O(log n)
```

---

# B+Tree

Virtually everything is stored as a tree.

```
Directories

Inodes

Snapshots

Object Map

Free Space

Extents

XAttrs
```

This greatly simplifies the kernel code.

---

# Object Map

Similar to APFS.

```
Object ID

↓

Physical Block
```

Thanks to this, there is no need to rewrite the entire disk after a Copy-on-Write operation.

---

# Copy-on-Write

If a block changes

```
old block

↓

new one created

↓

parent starts referencing the new one
```

The old block remains untouched.

---

# Checkpoints

After every transaction

```
new checkpoint

↓

new superblock
```

In the event of a failure, simply opening the last checkpoint suffices.

---

# Snapshots

A snapshot is simply a reference to an old tree root.

```
Snapshot

↓

Root Tree

↓

Directories

↓

Files
```

Since Copy-on-Write is used, the snapshot has almost zero overhead.

---

# Free Space Tree

Free space is also stored in a B+Tree. Each record

```
Start

Length
```

For example

```
2000

500 blocks
```

---

# Checksums

All metadata uses CRC64 or XXH3.

File data can be made optional.

---

# Compression

Optional.

```
None

LZ4

ZSTD
```

---

# Encryption

Each file can have its own key.

Or a single key can be used for the entire container.

---

# Containers

Like APFS.

```
Disk

↓

Container

↓

Volume A

Volume B

Volume C
```

All volumes share the same free space.

---

# Versioning

```
Major

Minor

Feature Flags
```

A new version can seamlessly open older partitions.

---

The result is a modern file system that feels like a blend of **APFS**, **btrfs**, and a bit of **ZFS**, yet remains simple enough for a single person to actually implement.

# Extent Reference Count

Each extent contains a reference count.

```c
struct extent {

object_header hdr; 

u64 physical_block; 

u64 block_count; 

u32 reference_count;

};
```

## How It Works

If multiple files use the same extent, the data is not physically copied.

```
File A
│
│
▼
Extent #125 (refcount = 2)
▲
│
File B
```

When writing to a file:

1. If `reference_count == 1`, the extent is modified using Copy-on-Write.
2. If `reference_count > 1`, a new extent is created, data is copied only for the modified portion, and reference counts are updated.

This enables the implementation of:

* fast `clone()`;
* deduplication;
* instant copying of large files.

---

# Journal

Since the file system uses Copy-on-Write, a full journal is not required.

The journal is used only for:

* updating the Superblock;
* publishing a new Checkpoint;
* committing a transaction.

All other changes are already atomic thanks to Copy-on-Write.

```
Modify metadata

↓

Allocate new blocks

↓

Write new metadata

↓

Write checkpoint

↓

Update superblock

↓

Commit
```

After a crash, simply opening the last valid Checkpoint is sufficient.

---

# Object Model

All file system structures are objects.

Each object has a unique `object_id` and begins with a common header.

```c
struct object_header {

u64 object_id; 

u64 transaction_id; 

u32 type; 

u32 flags; 

u64 checksum;

};
```

Any object is read in the same way:

```
Read block

↓

Read object_header

↓

Verify checksum

↓

Switch(type)

↓

Parse object
```

Supported object types:

| Type | Object           |
| ---- | ---------------- |
| 1    | Superblock       |
| 2    | Inode            |
| 3    | Directory B+Tree |
| 4    | Extent           |
| 5    | Extent Tree      |
| 6    | Object Map       |
| 7    | Snapshot         |
| 8    | Checkpoint       |
| 9    | Free Space Tree  |
| 10   | XAttr Tree       |

New types can be added without altering the existing format.

---

# Feature Flags

The Superblock contains a list of supported filesystem features.

```c
enum filesystem_features {

FEATURE_COMPRESSION    = 1 << 0,

FEATURE_ENCRYPTION     = 1 << 1,

FEATURE_CASE_SENSITIVE = 1 << 2,

FEATURE_VERITY         = 1 << 3,

FEATURE_SNAPSHOTS      = 1 << 4,

FEATURE_CLONES         = 1 << 5,

};
```

Superblock fields:

```c
u64 compatible_features;

u64 read_only_features;

u64 incompatible_features;
```

## Categories

### Compatible

Older drivers can safely ignore these features.

Examples:

* additional attributes;
* new metadata types.

### Read-Only Compatible

An older driver version can mount the filesystem in read-only mode.

Examples:

* new compression algorithms;
* new checksum storage methods.

### Incompatible

An older driver version must not mount the filesystem.

Examples:

* changes to the B+Tree format;
* changes to the Inode structure;
* changes to the Object Map.

---

This scheme is very similar to the one used by ext4, XFS, and btrfs, allowing the filesystem to evolve without "breaking" updates. Combined with the object model and CoW, this makes the architecture modular: new features are typically added as new object types or feature flags, without requiring a redesign of existing structures.
