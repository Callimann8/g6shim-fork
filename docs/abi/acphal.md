# AcpHal.dll

Observed from title 1738CA2A.

| Export | Observed |
|---|---|
| `AcpHalAllocateShapeContexts` | `HRESULT (ShapeRequest* request, void* reserved)`. `reserved` is null. Fills `request->array[k]` with zeroed arrays below 4 GiB. A failure makes audio libraries take their software path. |
| `AcpHalReleaseShapeContexts` | `HRESULT ()`. Called on shutdown when any array is non-null. |
| `AcpHalCreate` | `HRESULT (IAcpHal** out)`. |
| `ApuAlloc` | `HRESULT (void** address, UINT32* apu_address, UINT32 bytes, UINT32 alignment, UINT32 flags)`. Seen: `apu_address` null, 0x3000 bytes, alignment 4, flags 1. |
| `ApuFree` | `HRESULT (void* address)`. |
| `ApuMapVirtualAddress` | `UINT32 (const void* address)`. Returns the APU address of an `ApuAlloc` block; it equals the virtual address. |

## ShapeRequest

| Offset | Size | Field |
|---|---|---|
| 0x00 | 6 × 4 | element count per kind |
| 0x18 | 6 × 8 | array pointer per kind, written by the call |

| Kind | Contents | Stride | Count seen |
|---|---|---|---|
| 0 | SRC | 32 | 512 |
| 1 | EQ / compressor | 160 | 0 |
| 2 | filter / volume | 32 | 0 |
| 3 | DMA | 16 | 1024 |
| 4 | XMA | 64 | 512 |
| 5 | PCM | 32 | 0 |

## IAcpHal

Not IUnknown-shaped. `this` in `rcx`.

| Slot | Call | Observed |
|---|---|---|
| 0 | `HRESULT Init(UINT32 command_entries, UINT32 message_entries)` | 0x400, 0x400 |
| 1 | `HRESULT Shutdown()` | The caller then polls slot 3 until a 0x80 message arrives, with no timeout. |
| 2 | `HRESULT Send(UINT32 command, UINT64 sequence, UINT32 target, const void* payload, UINT32 flags)` | `target` 0xFFFFFFFF, `flags` 0 |
| 3 | `bool NextMessage(Message* out)` | Tested in `al`; drained until false. |
| 4 | `UINT32 PendingMessages()` | Not called. |
| 5 | `Release()` | Return not checked. |
| 6-9 | | Not called. |

`sequence` comes from one counter per title, shared by all voices. Commands sent with sequence 0 get no type-0x10 message.

| Command | Payload | Sequence | Use |
|---|---|---|---|
| 1 | `{UINT32 entries, UINT32 list_apu_address, UINT64 0}` | 0 | Run a command list for one 128-sample block. Four per output tick. |
| 2 | `{UINT32 0xFE}` | 0 | Once after `Init`. |
| 7 | `{UINT32 xma_index}` | yes | Voice start; the title rewrites the voice's contexts after it completes. |
| 0x10 | `{UINT32 dma_index, UINT32 blocks}` | yes | The title consumed `blocks` blocks of a DMA ring. |
| 0x13 | `{UINT32 xma_index, UINT32 buffer}` | yes | XMA input `buffer` (0 or 1) was refilled. |

## Message

0x1C bytes.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | type |
| 0x10 | 8 | value |

| Type | Meaning for the title |
|---|---|
| 2 | One command-1 list ran; frees a submit slot. Every fourth completes a group. |
| 0x10 | Command with sequence `value` completed. A gap or a duplicate stalls the title's completed-sequence counter; sequence 0 drives it backwards. |
| 0x80 | Shutdown done. |
| 4, 5, 8, 0x20, 0x40 | Ignored. |

Every title wait on these counters is infinite.

## Command list

64-bit entries; bits 4:0 are the operation. At most 0x600 entries.

| Operation | Fields |
|---|---|
| 1 | Allocates a scratch block id in bits 45:33. |
| 2 (XMA) | bits 24:13 XMA context index, 37:25 scratch id for channel 0, 50:38 for channel 1 (same id when mono) |
| 0x10 (DMA) | bits 26:14 scratch id, 38:27 DMA context index |

A mono voice is `1, 2, 0x10`; stereo is `1, 1, 2, 0x10, 0x10`. The SRC context of a voice has the XMA context's index.

## XMA context

| Dword | Bits | Field |
|---|---|---|
| 0 | 11:0 | input buffer 0 size in 2048-byte packets |
| 0 | 20, 21 | input buffer 0, 1 valid; the processor clears a bit when it has used the buffer up |
| 0 | 26:22 | output buffer size in 256-byte units |
| 1 | 11:0 | input buffer 1 size in packets |
| 1 | 28:27 | sample rate: 24000, 32000, 44100, 48000 |
| 1 | 29 | stereo |
| 2 | 25:0 | start read offset in bits from the start of buffer 0; written at start and seek only |
| 5, 6 | | APU address of input buffers 0 and 1 |
| 7, 8 | | APU address of an output and a work buffer; the title never reads them |

Input is standard XMA2, two buffers used as a ring: 0, 1, 0, and so on.

## SRC context

| Dword | Bits | Field |
|---|---|---|
| 1 | 20:0 | pitch ratio × 2^18, rewritten in place when the pitch changes |
| 4 | 20:0 | the same value |

Output stays at the source rate; the title resamples and mixes in software.

## DMA context

One per channel.

| Dword | Bits | Field |
|---|---|---|
| 1 | 4:0 | read block, advanced by command 0x10 |
| 1 | 12:8 | write block |
| 1 | 16 | ring full |
| 2 | 31:9 | APU address of the ring |
| 3 | 4:0 | 3: four blocks |

A block is 128 32-bit float samples of one channel.
