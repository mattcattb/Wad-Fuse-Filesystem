# WAD filesystem

This repository contains the original C++ WAD library and FUSE adapter plus a
small TCP service used by Break My System.

## TCP service

Build and run the server:

```bash
make
PORT=7373 WAD_DATA_DIR=./data ./wadsrv-bin
```

The service uses one request per TCP connection. Every request and response has
a 28-byte, network-byte-order header:

```text
magic[4] | version:u16 | type:u16 | request-id:u64 |
metadata-length:u32 | body-length:u64
```

The header is followed by tab-separated UTF-8 metadata and then the exact raw
body length. Response metadata is JSON; reads and downloads use the raw body.
Protocol version 1 supports `PING`, `CREATE_ARTIFACT`, `INSPECT`, `TREE`,
`LIST`, `STAT`, `READ`, `READ_RANGE`, `MKDIR`, `PUT`, `RESET`, `DOWNLOAD`, and
`DELETE_ARTIFACT`.

`CREATE_ARTIFACT` uploads a WAD body under an artifact ID. The server owns both
`/data/artifacts/<id>/original.wad` and `working.wad`; every later command uses
that ID, never a client filesystem path. `PUT` similarly carries raw lump bytes.

Mutation commands operate on a temporary copy, validate the result, and
atomically replace the working WAD. `MKDIR` creates a WAD namespace, whose name
is limited to two characters by the eight-character `_START` marker format.
`PUT` creates a new lump with a name of up to eight characters; it does not
replace existing lumps.

## Production image

```bash
docker build -t wad-server .
docker run --rm -p 7373:7373 -v "$PWD/data:/data" wad-server
```

The runtime image runs as an unprivileged user and exposes port `7373`. It uses
a bounded worker pool and per-artifact read/write locks: different artifacts can
run concurrently, while mutations of the same artifact are serialized and
atomically replace `working.wad`.

## FUSE adapter

The original FUSE program remains under `wadfs`. On a Linux host with libfuse:

```bash
make -C libWad
make -C wadfs
mkdir exampleMountDir
./wadfs/wadfs -s examples/sample1.wad ./exampleMountDir
```
