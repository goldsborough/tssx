# Notes

## Dynamic TSSX/No-TSSX detection

Inside `accept`, we first create a small shared memory segment with only two atomic bools that indicate TSSX awareness. We then set the server flag to true in this segment. On the client side, inside connect, the client could detect this flag being set to true, set its own flag to true and then allocate the shared memory segment (maybe reverse roles). Inside write (or read then), e.g. on server side (o.B.d.A.):

1. Is the client's tssx flag set?
  1. If not, use `real_write` since the client is not using tssx.
  2. Else, have we attached (not allocated) the segment to our address space yet?
    1. If not, attach it.
    2. Either way, call `buffer_write`

This may make the very first write/read expensive, but makes the rest work fab.
