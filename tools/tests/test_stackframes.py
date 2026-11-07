"""tools/stackframes.py: stack objects passed to calls, and the smaller-than-the-original warning.

Hand-assembled functions (no game bytes). Run: .venv/Scripts/python -I tools/tests/test_stackframes.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import stackframes as sf  # noqa: E402

SUB_ESP = lambda n: bytes([0x83, 0xEC, n])       # sub esp, imm8
ADD_ESP = lambda n: bytes([0x83, 0xC4, n])       # add esp, imm8
LEA_EAX_ESP = lambda d: bytes([0x8D, 0x44, 0x24, d]) if d else bytes([0x8D, 0x04, 0x24])  # lea eax, [esp + d]
PUSH_EAX, PUSH_ESI, POP_ESI, RET = b"\x50", b"\x56", b"\x5e", b"\xc3"
MOV_EAX_ECX = b"\x8b\x01"                         # mov eax, [ecx]
CALL_SLOT3 = b"\xff\x50\x0c"                      # call [eax + 0xc]


def getkey_caller(local, saved_push=False):
    """sub esp, local; [push esi]; lea eax, [esp + (4 if push)]; push eax; mov eax, [ecx]; call [eax+0xc];
    [pop esi]; add esp, local; ret: passes a `local`-byte stack object to slot 3."""
    body = SUB_ESP(local) + (PUSH_ESI if saved_push else b"")
    body += LEA_EAX_ESP(4 if saved_push else 0) + PUSH_EAX + MOV_EAX_ECX + CALL_SLOT3
    return body + (POP_ESI if saved_push else b"") + ADD_ESP(local) + RET


def test_object_size_and_slot():
    r = sf.analyse(getkey_caller(16))
    assert r["locals"] == 16 and r["ids"] == [("v", 12)] and r["calls"] == [[(-16, 16)]], r


def test_saved_register_push_after_locals():
    # MSVC reserves the locals first, then pushes callee-saved registers: they must not shift the offsets.
    r = sf.analyse(getkey_caller(16, saved_push=True))
    assert r["calls"] == [[(-16, 16)]], r


def test_smaller_object_warns():
    # The runs46 bug: a 12-byte key where the original reserves 16 for slot 3 to write.
    w = sf.smaller_objects(getkey_caller(12), getkey_caller(16))
    assert len(w) == 1 and "12 bytes in ours, 16 in the original" in w[0] and "slot 3" in w[0], w


def test_same_or_larger_is_quiet():
    assert sf.smaller_objects(getkey_caller(16), getkey_caller(16)) == []
    assert sf.smaller_objects(getkey_caller(20), getkey_caller(16)) == []


def test_unpaired_calls_are_not_compared():
    # Different callee (slot 4 vs slot 3): nothing lines up, so no warning.
    other = getkey_caller(12).replace(CALL_SLOT3, b"\xff\x50\x10")
    assert sf.smaller_objects(other, getkey_caller(16)) == []


def seh_caller(local):
    """An SEH frame (push ebp; mov ebp, esp; push -1; push handler; mov eax, fs:[0]; push eax; sub esp, local;
    push esi) that passes [ebp - 0x10 - local] (the bottom `local` bytes of its locals) to slot 3."""
    body = b"\x55\x8b\xec\x6a\xff" + b"\x68\x00\x00\x00\x00" + b"\x64\xa1\x00\x00\x00\x00" + PUSH_EAX
    body += SUB_ESP(local) + PUSH_ESI
    body += bytes([0x8D, 0x45, (-(0x0c + local)) & 0xFF])     # lea eax, [ebp - 0xc - local]
    body += PUSH_EAX + MOV_EAX_ECX + CALL_SLOT3
    return body + POP_ESI + b"\x8b\xe5\x5d" + RET             # pop esi; mov esp, ebp; pop ebp; ret


def test_direct_calls_pair_by_pushed_bytes():
    # A direct call with one argument doesn't pair with one taking two (another function, or another overload).
    call = b"\xe8\x00\x00\x00\x00"
    ours = SUB_ESP(16) + b"\xc7\x44\x24\x04" + bytes(4) + LEA_EAX_ESP(0) + PUSH_EAX + call + ADD_ESP(16) + RET
    orig = SUB_ESP(16) + b"\x6a\x00" + LEA_EAX_ESP(4) + PUSH_EAX + call + ADD_ESP(16) + RET
    assert sf.analyse(ours)["calls"] == [[(-16, 4)]] and sf.analyse(orig)["calls"] == [[(-16, 16)]]
    assert sf.smaller_objects(ours, orig) == []
    same = SUB_ESP(16) + LEA_EAX_ESP(0) + PUSH_EAX + call + ADD_ESP(16) + RET   # one argument too: compared
    assert len(sf.smaller_objects(ours, same)) == 1


def test_seh_frame_has_locals():
    # The SEH prologue's `push eax` (the previous registration) is not an argument: the locals after it count.
    r = sf.analyse(seh_caller(16))
    assert r["locals"] == 16 and r["calls"] == [[(-32, 16)]], r
    w = sf.smaller_objects(seh_caller(12), seh_caller(16))
    assert len(w) == 1 and "12 bytes in ours, 16 in the original" in w[0], w


def test_seh_frame_with_cookie():
    # /GS in an EH frame: `mov eax, [cookie]; xor eax, ebp; push eax` after the saved registers is not an argument.
    code = seh_caller(16)
    at = code.index(PUSH_ESI) + 1
    code = code[:at] + b"\xa1\x00\x00\x00\x00\x33\xc5" + PUSH_EAX + code[at:]
    code = code.replace(POP_ESI + b"\x8b\xe5", b"\x58" + POP_ESI + b"\x8b\xe5")   # pop eax (the cookie) first
    r = sf.analyse(code)
    assert r["locals"] == 16 and r["calls"] == [[(-32, 16)]], r


def test_read_only_offsets_are_not_boundaries():
    # After the call the function reads a field of the object the callee filled ([esp + 8]): a field, not a boundary.
    code = getkey_caller(16).replace(ADD_ESP(16), b"\x8b\x44\x24\x08" + ADD_ESP(16))
    assert sf.analyse(code)["calls"] == [[(-16, 16)]]
    # A write there is a boundary (a second object).
    code = getkey_caller(16).replace(ADD_ESP(16), b"\x89\x44\x24\x08" + ADD_ESP(16))
    assert sf.analyse(code)["calls"] == [[(-16, 8)]]


def test_sse_store_is_a_write():
    # movups xmmword ptr [esp + 4], xmm0 (capstone marks its memory operand as read): a boundary like any store.
    code = SUB_ESP(16) + b"\x0f\x11\x44\x24\x04" + getkey_caller(16)[3:]
    assert sf.analyse(code)["calls"] == [[(-16, 4)]], sf.analyse(code)


def test_code_after_an_early_return():
    # sub esp, 16; push esi; test ecx, ecx; je L; pop esi; add esp, 16; ret;
    # L: lea eax, [esp + 4]; push eax; mov eax, [ecx]; call [eax + 0xc]; pop esi; add esp, 16; ret
    code = SUB_ESP(16) + PUSH_ESI + b"\x85\xc9\x74\x05" + POP_ESI + ADD_ESP(16) + RET
    code += LEA_EAX_ESP(4) + PUSH_EAX + MOV_EAX_ECX + CALL_SLOT3 + POP_ESI + ADD_ESP(16) + RET
    assert sf.analyse(code)["calls"] == [[(-16, 16)]], sf.analyse(code)


def test_cdecl_cleanup_after_other_instructions():
    # sub esp, 16; push 1; push 2; call f; mov ecx, eax; add esp, 8 (the cdecl cleanup, not right after the call);
    # lea eax, [esp]; push eax; mov eax, [ecx]; call [eax + 0xc]; add esp, 16; ret
    code = SUB_ESP(16) + b"\x6a\x01\x6a\x02\xe8\x00\x00\x00\x00\x8b\xc8" + ADD_ESP(8)
    code += LEA_EAX_ESP(0) + PUSH_EAX + MOV_EAX_ECX + CALL_SLOT3 + ADD_ESP(16) + RET
    assert sf.analyse(code)["calls"] == [[], [(-16, 16)]], sf.analyse(code)


CALL_SLOT4 = b"\xff\x50\x10"                      # call [eax + 0x10]


def test_scoped_shared_slot():
    # Two scopes share a slot: slot 3 gets [esp + 4] (a scratch), then slot 4 gets [esp] (16 bytes). Unscoped, the
    # scratch's lea bounds the second object at 4 bytes; scoped (our side), that lea lies outside its code: 16.
    code = SUB_ESP(16) + LEA_EAX_ESP(4) + PUSH_EAX + MOV_EAX_ECX + CALL_SLOT3
    code += LEA_EAX_ESP(0) + PUSH_EAX + MOV_EAX_ECX + CALL_SLOT4 + ADD_ESP(16) + RET
    assert sf.analyse(code)["calls"][1] == [(-16, 4)]
    assert sf.analyse(code, scoped=True)["calls"][1] == [(-16, 16)], sf.analyse(code, scoped=True)
    assert sf.smaller_objects(code, code) == []
    # Both passed to one call: the scratch's lea lies within the object's code, so it still bounds it.
    code = SUB_ESP(16) + LEA_EAX_ESP(0) + PUSH_EAX + bytes([0x8D, 0x44, 0x24, 0x08]) + PUSH_EAX
    code += MOV_EAX_ECX + CALL_SLOT3 + ADD_ESP(16) + RET
    assert sorted(sf.analyse(code, scoped=True)["calls"][0]) == [(-16, 4), (-12, 12)], sf.analyse(code, scoped=True)


def test_scoped_constructor_fields():
    # mov [esp], 0; mov [esp + 4], 0; mov [esp + 8], 0 (one object's fields), then its address to slot 3.
    fill = b"\xc7\x04\x24" + bytes(4) + b"\xc7\x44\x24\x04" + bytes(4) + b"\xc7\x44\x24\x08" + bytes(4)
    code = SUB_ESP(16) + fill + LEA_EAX_ESP(0) + PUSH_EAX + MOV_EAX_ECX + CALL_SLOT3 + ADD_ESP(16) + RET
    assert sf.analyse(code)["calls"] == [[(-16, 4)]]
    assert sf.analyse(code, scoped=True)["calls"] == [[(-16, 16)]], sf.analyse(code, scoped=True)
    # the same writes far apart are separate objects
    spread = fill[:7] + b"\x90" * 4 + fill[7:15] + b"\x90" * 4 + fill[15:]
    code = SUB_ESP(16) + spread + LEA_EAX_ESP(0) + PUSH_EAX + MOV_EAX_ECX + CALL_SLOT3 + ADD_ESP(16) + RET
    assert sf.analyse(code, scoped=True)["calls"] == [[(-16, 4)]], sf.analyse(code, scoped=True)
    # a later run rewriting fields only (a string's reset: [esp + 8], [esp + 4]) starts no object of its own
    reset = b"\x90" * 4 + b"\xc7\x44\x24\x08" + bytes(4) + b"\xc7\x44\x24\x04" + bytes(4)
    code = SUB_ESP(16) + fill + reset + LEA_EAX_ESP(0) + PUSH_EAX + MOV_EAX_ECX + CALL_SLOT3 + ADD_ESP(16) + RET
    assert sf.analyse(code, scoped=True)["calls"] == [[(-16, 16)]], sf.analyse(code, scoped=True)


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
