# Notes

## Dynamic TSSX/No-TSSX detection

### First Approach

Inside `accept`, we first create a small shared memory segment with only two atomic bools that indicate TSSX awareness. We then set the server flag to true in this segment. On the client side, inside connect, the client could detect this flag being set to true, set its own flag to true and then allocate the shared memory segment (maybe reverse roles). Inside write (or read then), e.g. on server side (o.B.d.A.):

1. Is the client's tssx flag set?
  1. If not, use `real_write` since the client is not using tssx.
  2. Else, have we attached (not allocated) the segment to our address space yet?
    1. If not, attach it.
    2. Either way, call `buffer_write`

This may make the very first write/read expensive, but makes the rest work fab.

### Second Approach

The above will probably not work properly because the shared memory key must be
known by both (all processes), since we can't pass the key via a socket as we
do for the connection segments (where we know that the other process is
cooperating). Rather, we'll have to have a global "registry" file (as in, a
document file in the file system) where we associate the domain socket path with
two bits for the client and server. In the simplest case, this could be a file
`/tmp/tssx.registry` with the following format:

```
<domain socket path> <server bit> <client bit>
```

However, this would require linear lookups and in-file stream processing as the
file would have to be synchronized with the connections being opened and closed
by any process. Much rather, we could simply the two bits with every socket in a
*separate file*. For example, say the domain socket path is
`/tmp/domain-socket`. Then we'll have a `/tmp/registry/domain-socket` or
`/tmp/domain-socket.tssx` file, storing just a single byte (for the two
bits). This would allow very fast lookup and updates, since we have a direct
bijection between domain sockets and the associated registry files. Then, on
either side, we would check if the registry file exist and optionally create
it. Either way, we then load the file and update one of the two bits according
to the side on which we are (server/client). For this, we'd need a file-locking
mechanism (posix file locks) to ensure synchronization. But basically, we'd have
to first check to create the registry, and then update it:

```
if (registry does not exist yet) {
	result = try_to_create_registry();
	if (result == ERROR) return error;
	// else we either sucessfully created the registry (folder/file)
	// or we had a race condition but the other side was quicker to
	// create the file in exlusive mode
}

lock_file();
bits = load_registry_bits();
bits |= bit_for_this_side;
store_registry_bits(bits);
unlock_file();
```

We could then check the bits the first time we `write()` or `read()`. We just
have to be really careful about race conditions.

### Third Approach

Shared Memory Segments could indeed work, if we simply use the `ftok` generated
key that deterministically transforms a file path (i.e. the domain socket path)
to a key, suitable for shared memory segments. Thus, this would probably be the
better approach. Again, in `accept` and `connect` we optionally create or
retrieve the shared memory segment and set the server or client's bit. Then, in
`read` and `write`, we would have to do atomic loads to see if the client/server
has set their bit, and if so store that boolean into a non-atomic boolean for
faster loads. This shared data structure could be placed into the `Connection`
struct.
