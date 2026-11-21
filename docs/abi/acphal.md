# AcpHal.dll

Observed from title 1738CA2A.

| Export | Observed |
|---|---|
| `AcpHalAllocateShapeContexts` | `HRESULT (ShapeRequest* request, void* reserved)`. `reserved` is null. Fills `request->array[k]` with zeroed arrays below 4 GiB. A failure makes audio libraries take their software path. |
| `AcpHalReleaseShapeContexts` | `HRESULT ()`. Called on shutdown when any array is non-null. |
| `AcpHalCreate` | `HRESULT (IAcpHal** out)`. |
| `ApuAlloc` | `HRESULT (void** address, UINT32* apu_address, UINT32 bytes, UINT32 alignment, UINT32 flags)`. Seen: `apu_address` null, 0x3000 bytes, alignment 4, flags 1. |
| `ApuFree` | `HRESULT (void* address)`. |

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
