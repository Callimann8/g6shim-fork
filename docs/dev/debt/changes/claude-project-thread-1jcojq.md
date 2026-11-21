# acphal

## remove `AcpHalAllocateShapeContexts` fails

## change `AcpHalAllocateShapeContexts` takes two pointers

- The EQ/compressor, filter/volume and PCM shape contexts are allocated but never processed; no title seen uses them.
  Title IDs: 1738CA2A

## add Build

- XMA decoding needs FFmpeg headers at build time (`G6SHIM_FFMPEG_INCLUDE_DIR`) and `avcodec`/`avutil` DLLs of the same major version at run time; without them XMA voices play silence.

## add Silent stubs

- `IAcpHal::Send` command 2 succeeds without effect.
  Title IDs: 1738CA2A
- Command-list operation 1 is ignored.
  Title IDs: 1738CA2A

## add Unknown function info

- `IAcpHal` slots 4 and 6-9, `Send`'s `target` and `flags`, and the XMA context's output and work buffers are unconfirmed.
  Title IDs: 1738CA2A

## add Functional

- The processor runs all queued commands every 256 samples at 48 kHz; the console's frame size and cadence are unknown.
  Title IDs: 1738CA2A
- `ApuIsVirtualAddressValid` is a stub.
