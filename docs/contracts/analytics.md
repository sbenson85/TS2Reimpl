# Contract: launch analytics event (0xc2df67)

**Contract: approved by the user (2026-10-11).** Hooks enabled in-game. Status: `replaced`. Source: `src/app/WinMainInfra.{h,cpp}` (`winmaininfra::SendAnalyticsEvent`).
Tests: `tests/winmain_infra_crash.cpp` (`WinMainInfra_SendAnalyticsEvent`). Hook: `WinMainInfra_SendAnalyticsEvent`.

## 1. Interface

`void __cdecl SendAnalyticsEvent(uint32_t steamIdLow, uint32_t steamIdHigh)`

- `__cdecl`, two stack arguments: the 64-bit Steam user id as two words (low first). The caller
  pops them.
- No result. The original leaves `eax` undefined (the value of its last call) and WinMain ignores it.
- Size in the image: 609 bytes. It calls 0xc2de96 (the event body builder, 10 bytes before its
  `_EH_prolog`) and a family of small helpers that are only reachable from here.

## 2. What the original does

1. `InternetOpenA("Mozilla/4.0 (compatible)", 0, 0, 0, 0)`. If that fails it returns at once, with
   nothing else done. (This is the path the host already forces: `src/hook/analytics.cpp` makes
   exactly that call fail so no launch event leaves the machine.)
2. Builds the event body with 0xc2de96: a hash of the Steam id (32 bytes in hex), the current UTC
   time (`strftime`), two words read from globals (RVAs 0x1081ff8, 0x1081ffc), and the local address
   literal `127.0.0.1`, formatted with `vsprintf_s` into the global buffer at RVA 0x10fe048 (size
   0x400).
3. `InternetConnectA` to an EA data host on port 443, `HttpOpenRequestA` (`POST`, `/genericEvents`,
   secure), `HttpAddRequestHeadersA` (`x-ea-app-type: freeform`), `HttpSendRequestExA`,
   `InternetWriteFile` of the body (looped), `HttpEndRequestA`, `InternetReadFile` (up to 100 bytes of
   the answer), `HttpQueryInfoA` (status).
4. Two write-only statics, each set once through a thread-safe-static guard: at RVA 0x10fe448 the
   first byte of the status text (when `HttpQueryInfoA` worked), at 0x10fe450 the thread's
   `GetLastError()` (when the send itself failed). Guards at 0x10fe44c and 0x10fe454.
5. Closes the handles and wipes the body buffer with `memset(buffer, 0, strlen(buffer))`.

External effects: one HTTPS POST to a third-party analytics service carrying a hash of the player's
Steam id, a time stamp and the storefront; WinINet state in the process; the global buffer and two
write-only statics.

## 3. What the rest of the game reads back

Nothing. Searched with `tools/xrefs.py` and a scan of every relocated reference into
0x10fe040..0x10fe460:

- The body buffer (0x10fe048..0x10fe447) is referenced only by 0xc2de96. It is wiped after the send, so
  after a completed call it holds zeros, which is what it holds at start-up and what it holds if the
  call returns early (the host's current behaviour).
- The statics at 0x10fe448 and 0x10fe450, and their guards, are referenced only by 0xc2df67 itself.
  Nothing reads the values.
- 0x10fe040/0x10fe020, which sit next to them, belong to `ShowFatalMessage`'s separator string and
  are unrelated.
- Gameplay, saves, mods: no effect. The Steam id is only an input to the hash. WinINet's
  process-wide state is not used elsewhere: 0xc2df67 is the only function in the image that calls any
  of the ten WinINet imports (db/functions.jsonl).

## 4. Contract

### Kept

Nothing. There is no state the game reads back.

(The caller, WinMain, still looks up the Steam id through the Steam context at RVA 0x1081a7c before
calling this; that context is read only there. It is WinMain's business and stays while WinMain
stays the original; see the WinMain notes in `db/status/WinMainInfra.jsonl`.)

### Dropped, with reasons

| Dropped | Reason |
|---|---|
| The HTTPS POST and all WinINet use | The project does not send player data to third parties (CLAUDE.md; the host already blocks it). |
| The hash of the Steam id and the event body | Only the request's content. |
| The write to the buffer at 0x10fe048 | It is wiped again before the original returns; omitting it leaves the same bytes. |
| The two write-only statics and their guards | Nothing reads them. |

### Judgement calls (approved 2026-10-11)

1. The function does nothing at all, rather than keeping a local log of "a launch happened". The
   original keeps no local record, so adding one would be new behaviour.
2. The Steam id parameters are kept in the signature only so the hook and the caller keep their
   ABI. They are unused.

## 5. Tests

`WinMainInfra_SendAnalyticsEvent`. All WinINet entry points and `GetSystemTimeAsFileTime` are faked
in the original's import table.

- Blocked path (the host's, `InternetOpenA` fails), five Steam ids: the original makes exactly one
  call (`InternetOpenA`); ours makes none; a checksum of the buffer and the statics
  (0x10fe048..0x10fe457) is unchanged by either.
- Sending path, three endings (status query fails, status query works, the send fails), with the
  original really running its body builder and sender: the original reaches the send and the write
  calls (so the test would notice if it never sent); afterwards the body buffer is all zeros; the
  statics are written exactly in the modes that write them (documenting that they are write-only); ours
  in the same state makes no call and leaves the statics untouched.

Mutation check: a mutant of ours that writes to the buffer is killed. There is no other behaviour to
mutate.
