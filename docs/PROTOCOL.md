# The Ninjam wire protocol, as Antiphon implements it

Reference material for writing or changing parser code. This is the wire format
only -- no dispatch, no state. The code that owns it is
`src/NinjamProtocol.{h,cpp}`, which is pure by design (no sockets, no threads, no
shared state) so it can be exercised directly with deliberately malformed input.

**Authoritative sources**: `references/ninjam/ninjam/mpb.{h,cpp}` for message
constants and layouts, `references/ninjam/ninjam/server/usercon.cpp` for
server-side behaviour, `references/ninjam/ninjam/njclient.cpp` for client-side
flows. Read `references/ninjam.md` first.

---

## Framing

Every message: **1-byte type + 4-byte little-endian payload length + payload**.

```
+--------+---------------------------+------------------+
| type   | length (uint32 LE)        | payload          |
| 1 byte | 4 bytes                   | `length` bytes   |
+--------+---------------------------+------------------+
```

`kHeaderSize = 5`. `kMaxPayload = 10 MiB`; `readFrameHeader` rejects anything
larger rather than attempting the allocation.

## Endianness

**All multi-byte integers in this protocol are little-endian.** This includes the
ones that look like they should not be: BPM and BPI in `0x02`
(`mpb.cpp:192-195`), and channel volume in `0x03` (`mpb.cpp:281-282`).

This is worth stating loudly because our own documentation had it wrong -- it
claimed `0x02`/`0x03` integers were big-endian for months while the code was
right. If you are reading a value that looks wildly out of range, check this
first.

---

## Message table

| Msg | Dir | Hex | Payload |
|---|---|---|---|
| `SERVER_AUTH_CHALLENGE` | S->C | `0x00` | 8-byte challenge, then more we do not read |
| `SERVER_AUTH_REPLY` | S->C | `0x01` | 1-byte flag (1 = granted), then a NUL-terminated error message, then a 1-byte `maxchan`. **See below.** |
| `SERVER_CONFIG_CHANGE` | S->C | `0x02` | 2-byte BPM + 2-byte BPI, uint16 LE each |
| `USER_INFO_CHANGE` | S->C | `0x03` | List of records; **6-byte fixed part**, then two strings. See below. |
| `DOWNLOAD_INTERVAL_BEGIN` | S->C | `0x04` | 16-byte GUID + 4-byte estSize LE + 4-byte fourCC + 1-byte chIdx + NUL-term username |
| `DOWNLOAD_INTERVAL_WRITE` | S->C | `0x05` | 16-byte GUID + 1-byte flags (bit 0 = final) + Ogg payload |
| `CLIENT_AUTH_USER` | C->S | `0x80` | 20-byte SHA1 + NUL-term username + 4-byte caps LE + 4-byte version LE |
| `CLIENT_SET_USERMASK` | C->S | `0x81` | List of: NUL-term username + 32-bit channel bitmask LE |
| `CLIENT_SET_CHANNEL_INFO` | C->S | `0x82` | 2-byte LE mpisize (= 4), then per channel: NUL-term name + 2-byte LE volume + 1-byte pan + 1-byte flags |
| `UPLOAD_INTERVAL_BEGIN` | C->S | `0x83` | Exactly 25 bytes: 16-byte GUID + 4-byte estSize LE + 4-byte fourCC + 1-byte chIdx. **No username string.** |
| `UPLOAD_INTERVAL_WRITE` | C->S | `0x84` | Identical layout to `0x05` |
| `CHAT_MESSAGE` | S<->C | `0xC0` | 5 NUL-terminated strings: type, then four type-specific params |
| `KEEP_ALIVE` | C->S | `0xFD` | Zero-byte payload; send every 3 s |

---

## The messages with traps in them

### `0x01` SERVER_AUTH_REPLY -- the `maxchan` tail

The obvious reading of this message is "one byte: granted or not". That reading
is wrong in a way that is invisible until a *different* client talks to you.

The full payload is `flag`, then a NUL-terminated error message, then a 1-byte
**`maxchan`**: the maximum local channel index the server will accept. The
reference client refuses to transmit on any channel at or above this value
(`njclient.cpp:1096`, `:1476`), so a server that omits the tail gets **no audio
at all** from a stock client -- it reads `maxchan = 0` and concludes it may not
transmit on any channel.

This is exactly the self-referential blind spot differential testing exists to
find: our `FakeNinjamServer` omitted the tail, our own client did not care, and
the bug only surfaced when the reference client refused to send us anything.
`buildAuthReply()` therefore defaults to `maxChannels = 32`.

Older servers genuinely omit it, so a missing tail is not an error -- it parses
as absent and `maxChannels` stays 0.

### `0x03` USER_INFO_CHANGE -- the fixed part is 6 bytes, not 4

Each record is:

```
active (u8) | chIdx (u8) | volume (int16 LE) | pan (int8) | flags (u8)
             | username (NUL-term) | channelName (NUL-term)
```

That fixed part is **6 bytes**. Reading it as 4 was an off-by-two that shifted
every subsequent string read, and since a `MemoryBlock` is sized to exactly the
payload and is *not* NUL-padded, the resulting unbounded string reads walked the
heap. It reproduced as a heap-buffer-overflow under ASan.

Records that parse successfully before a malformed one are retained, matching the
reference server's forgiving treatment of trailing garbage.

Antiphon responds to every `0x03` by sending `CLIENT_SET_USERMASK` (`0x81`) to
subscribe to the channels it wants.

### `0x04` / `0x83` -- fourCC decides everything

The 4-byte fourCC is the payload type:

| fourCC | Meaning | We do |
|---|---|---|
| `OGGv` | Ogg/Vorbis audio | Decode and play |
| `JTBv` | Jamtaba H.264 video | **Ignore** |
| anything else | Unknown extension | Ignore |

Before the filter existed, `JTBv` intervals were queued as audio, creating a
permanently silent channel stream that swapped every interval forever. The server
routes these opaquely and does not decode them, so anything can appear here --
treat an unrecognised fourCC as "not for us", never as an error. See
`NON-GOALS.md` fence #1 and `DESIGN.md` §14.

`0x83` (upload) is **exactly 25 bytes** and carries no username string; `0x04`
(download) carries one. `parseIntervalBegin` handles both forms.

### `0x80` CLIENT_AUTH_USER -- the double hash

```
hash = SHA1( SHA1(username + ":" + password) + challenge[0..8] )
```

Caps = 1, version = `0x00020000`. The hash is 20 bytes, followed by the
NUL-terminated username, then caps and version as LE uint32s.

`Sha1.{h,cpp}` is first-party (minimal FIPS 180-1); only `add` + `result` are
used. The incremental `add` is tested to equal the monolithic result at every
split point, because this path depends on it.

### `0x81` CLIENT_SET_USERMASK -- also the Recv toggle

Bit N of the 32-bit LE mask means "subscribe to channel N of this user". Sent on
every `USER_INFO_CHANGE` to subscribe, and *also* the mechanism behind the
per-channel **Recv** button: clearing the bit makes the server stop forwarding
that channel entirely, which is a bandwidth control rather than a mix control
(`DESIGN.md` §7).

Channel indices >= 32 are dropped rather than shifted, since `1u << 32` is
undefined behaviour.

### `0x82` CLIENT_SET_CHANNEL_INFO -- mpisize

The leading `uint16 LE` is the size of the per-channel metadata block *after*
each name -- 4 for us (volume, pan, flags). The server reads exactly that many
bytes after each name, so getting it wrong desynchronises the whole record list.
The server then broadcasts a `USER_INFO_CHANGE` to everyone, which is how other
players see your channel names.

### `0xC0` CHAT_MESSAGE

Five NUL-terminated strings. The first is the type; the remaining four are
type-specific:

| Type | Meaning |
|---|---|
| `MSG` | Ordinary room message |
| `PRIVMSG` | Private message |
| `TOPIC` | Room topic |
| `JOIN` / `PART` | Player arrived / left |
| `ADMIN` | Admin command; also carries `!vote` results |

Voting (`!vote bpm <n>`, `!vote bpi <n>`), `/me`, `/topic`, `/kick` and `/msg`
are all sent through this message.

---

## Parsing rules

Everything goes through `NinjamProtocol::Reader`, a bounds-checked cursor.

- Every accessor returns `false` and leaves its output untouched if the read
  would run past the end.
- Once a read fails the cursor **latches** failed, so a caller may check `ok()`
  once at the end rather than after every field.
- `cstr()` reads up to the next NUL and fails, **without advancing**, if no NUL
  appears before the end of the payload. This is the accessor that makes
  truncated or hostile records safe: a `MemoryBlock` is sized to exactly the
  payload and is not NUL-padded, so an unbounded string read walks the heap.

**Five such over-reads existed in the `0x03` and `0xC0` parsers** before the
`Reader` was introduced. They are now impossible by construction.

### Testing requirement

Any change here goes into the **truncation sweep** in
`test/NinjamProtocolTests.cpp` `runTruncationTests()` -- every parser is fed
every truncation of every message -- not just a happy-path case. And it gets an
ASan run: without a sanitiser, a read past the end of a `MemoryBlock` usually
succeeds silently. Build instructions are in `test/README.md`; ignore UBSan
output from inside libvorbis, which is pre-existing and benign.
