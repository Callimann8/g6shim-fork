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
| 2 | `HRESULT Send(UINT32 command, UINT32 size, UINT32 target, const void* payload, UINT32 flags)` | `size` 0, `target` 0xFFFFFFFF, `flags` 0 |
| 3 | `bool NextMessage(Message* out)` | Tested in `al`; drained until false. |
| 4 | `UINT32 PendingMessages()` | Not called. |
| 5 | `Release()` | Return not checked. |
| 6-9 | | Not called. |

| Command | Payload | Sent |
|---|---|---|
| 1 | `{UINT32 count, UINT32 apu_address, UINT64 0}` | 4 per output tick, all pointing at one 0x3000-byte `ApuAlloc` buffer the title mixes into |
| 2 | `{UINT32 0xFE}` | Once after `Init` |

## Message

0x1C bytes.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | type |
| 0x10 | 8 | value |

| Type | Meaning for the title |
|---|---|
| 2 | One command-1 buffer is done; frees a submit slot. Every fourth completes a group. |
| 0x10 | Fence; `value` must count up by one per message. |
| 0x80 | Shutdown done. |
| 4, 5, 8, 0x20, 0x40 | Ignored. |

Voice removal in the title waits until the group count and fence pass the voice's targets, so missing type-2 or 0x10 messages hang it.
