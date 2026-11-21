# acphal

## change `AcpHalAllocateShapeContexts` fails

- `AcpHalAllocateShapeContexts` allocates the arrays, but nothing processes them until `AcpHalCreate` exists.
  Closes when: `AcpHalCreate` returns a processor that walks the arrays.
  Title IDs: 1738CA2A

## change `AcpHalAllocateShapeContexts` takes two pointers

- Shape-context entry layouts are unknown beyond their size.
  Title IDs: 1738CA2A

## add Functional

- `AcpHalCreate` fails with `E_NOTIMPL`; audio libraries then release the shape contexts.
  Title IDs: 1738CA2A
