# acphal

## remove `AcpHalAllocateShapeContexts` fails

## change `AcpHalAllocateShapeContexts` takes two pointers

- Shape-context entry layouts are unknown beyond their size; nothing reads the arrays.
  Title IDs: 1738CA2A

## add Silent stubs

- `IAcpHal::Send` command 1 completes buffers without playing them.
  Closes when: the buffers reach a WASAPI stream.
  Title IDs: 1738CA2A
- `IAcpHal::Send` command 2 succeeds without effect.
  Title IDs: 1738CA2A

## add Functional

- The processor completes command-1 buffers and posts one fence per 256-sample frame at 48 kHz; the console's frame size and cadence are unknown.
  Title IDs: 1738CA2A
- `ApuIsVirtualAddressValid` is a stub.
