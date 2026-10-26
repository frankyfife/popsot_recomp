"""Check every address in a target's target.h and target_hooks.c against its XBE.

    python tools/test-xbe-sites.py                  # both targets
    python tools/test-xbe-sites.py xbox-retail
    python tools/test-xbe-sites.py --uncovered xbox-demo

Each target has source/<target>/xbe_sites.txt: one checked claim per line,
written from what the code and its comments say about an address. The test
fails when a claim does not hold in the XBE, and when target.h or
target_hooks.c names an address (any hex literal of 0x10000 or more, code or
comment, and every sub_XXXXXXXX) that no line of xbe_sites.txt covers. Some
checks need only the generated source; the others need the user's XBE,
Assets/<target>/default.xbe (or POPSOT_XBE_<TARGET>, e.g. POPSOT_XBE_XBOX_DEMO).
Without it those checks are skipped and reported as skipped, except that a
"ret" claim is then checked in the generated code, which pushes each call's
return address; --require-xbe makes a missing XBE a failure. The XBE is only read, and its SHA-256 must match
POPSOT_XBE_SHA256 in target.h.

Claims (addresses in hex, "#" starts a note):
    func A          A starts a lifted function (gen/recomp_funcs.h, not an
                    unresolved stub), outside .data and .rdata
    entry A         A is the XBE entry point (retail key)
    title V         V is the certificate's title ID
    span A B        func A, and B lies before the next lifted function that
                    is not a known split
    split A         the lifter made a function at A inside another one; no
                    generated code calls it (only the dispatch table lists it)
    ret A F         the instruction ending at A is a call; F is its target,
                    or * for an indirect call
    call A F        the instruction at A is a call to F (direct, or through
                    the pointer F), or * for any call
    ref A V         the instruction at A has V as an immediate or as a memory
                    displacement
    insn A TEXT     the instruction at A starts with TEXT ("fptan", "rep movsd")
    code A          an instruction decodes at A in a code section (for
                    addresses a comment names without saying more)
    data A          A lies in a section of the XBE
    section A SIZE [END]
                    a section starts at A with virtual size SIZE (ending at END)
    u32 A V         the dword at A is V
    f32 A V         the float at A is V
    other T A       A belongs to target T's XBE: a lifted function of T
    value V         V is not an address in any XBE (a register, key, mask)
"""
import hashlib
import os
import re
import struct
import sys
from pathlib import Path

import capstone

ROOT = Path(__file__).resolve().parent.parent
TARGETS = ("xbox-demo", "xbox-retail")
LITERAL = re.compile(r"\b0x([0-9A-Fa-f]+)[uUlL]*\b")
SUB_NAME = re.compile(r"\bsub_([0-9A-Fa-f]{8})(?:_enter|_gen)?\b")
ENTER_DEF = re.compile(r"^void sub_([0-9A-Fa-f]{8})_enter\(void\)", re.M)
FUNC_DECL = re.compile(r"^void sub_([0-9A-Fa-f]{8})\(void\);", re.M)
STUB_DEF = re.compile(r"^void sub_([0-9A-Fa-f]{8})\(void\)", re.M)


RETAIL_ENTRY_KEY = 0xA8FC57AB


class Xbe:
    def __init__(self, data):
        self.data = data
        base = struct.unpack_from("<I", data, 0x104)[0]
        count, headers = struct.unpack_from("<II", data, 0x11C)
        self.sections = []
        for i in range(count):
            flags, va, vsize, raw, rsize, name = struct.unpack_from("<6I", data, headers - base + i * 56)
            name = data[name - base:name - base + 16].split(b"\0")[0].decode("ascii", "replace")
            self.sections.append((va, vsize, raw, rsize, name))
        self.entry = struct.unpack_from("<I", data, 0x128)[0] ^ RETAIL_ENTRY_KEY
        cert = struct.unpack_from("<I", data, 0x118)[0]
        self.title = struct.unpack_from("<I", data, cert - base + 8)[0]
        self.cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.cs.detail = True

    def section(self, va):
        for s in self.sections:
            if s[0] <= va < s[0] + s[1]:
                return s
        return None

    def read(self, va, size):
        s = self.section(va)
        if not s:
            raise ValueError("0x%08X is not in any section" % va)
        off = va - s[0]
        chunk = self.data[s[2] + off:s[2] + min(off + size, s[3])]
        return chunk + b"\0" * (size - len(chunk))

    def insn(self, va):
        for i in self.cs.disasm(self.read(va, 16), va, 1):
            return i
        raise ValueError("no instruction decodes at 0x%08X" % va)

    def code(self, va):
        # Every section of these XBEs carries the executable flag, .data too.
        s = self.section(va)
        return bool(s and s[4] not in (".data", ".rdata"))


def operand_values(i):
    values = set()
    for op in i.operands:
        if op.type == capstone.x86.X86_OP_IMM:
            values.add(op.imm & 0xFFFFFFFF)
        elif op.type == capstone.x86.X86_OP_MEM:
            values.add(op.mem.disp & 0xFFFFFFFF)
    return values


def call_target(i):
    """(direct target, pointer) of a call instruction; None where unknown."""
    if i.mnemonic != "call":
        raise ValueError("0x%08X is %s %s, not a call" % (i.address, i.mnemonic, i.op_str))
    op = i.operands[0]
    if op.type == capstone.x86.X86_OP_IMM:
        return op.imm & 0xFFFFFFFF, None
    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
        return None, op.mem.disp & 0xFFFFFFFF
    return None, None


def call_ending_at(xbe, va):
    for length in (5, 6, 2, 3, 7):
        try:
            i = xbe.insn(va - length)
        except ValueError:
            continue
        if i.size == length and i.mnemonic == "call":
            return i
    raise ValueError("no call instruction ends at 0x%08X" % va)


def number(text):
    return int(text, 16)


def same_function(name, value):
    return name == "*" or number(name.replace("sub_", "")) == value


class Target:
    def __init__(self, name):
        self.name = name
        self.dir = ROOT / "source" / name
        gen = self.dir / "gen"
        self.functions = {number(m) for m in FUNC_DECL.findall((gen / "recomp_funcs.h").read_text())}
        stubs = gen / "recomp_stubs_unresolved.c"
        if stubs.exists():
            self.functions -= {number(m) for m in STUB_DEF.findall(stubs.read_text())}
        self.sorted = sorted(self.functions)
        self.gen_text = None
        hooks = (self.dir / "target_hooks.c").read_text()
        self.manual = ({number(m) for m in STUB_DEF.findall(hooks)}
                       - {number(m) for m in re.findall(r"^extern void sub_([0-9A-Fa-f]{8})_gen\(void\);", hooks, re.M)})

    def hand_written(self, va):
        """Whether va lies in a function target_hooks.c replaces outright."""
        starts = [f for f in self.sorted if f <= va]
        return bool(starts) and starts[-1] in self.manual

    def generated(self):
        if self.gen_text is None:
            self.gen_text = "\n".join(p.read_text(errors="replace") for p in (self.dir / "gen").rglob("*.c"))
        return self.gen_text

    def sha256(self):
        m = re.search(r'POPSOT_XBE_SHA256\s+"([0-9A-Fa-f]{64})"', (self.dir / "target.h").read_text())
        return m.group(1).upper()

    def xbe_path(self):
        env = os.environ.get("POPSOT_XBE_" + self.name.upper().replace("-", "_"))
        return Path(env) if env else ROOT / "Assets" / self.name / "default.xbe"


def load_claims(target):
    claims = []
    for n, line in enumerate((target.dir / "xbe_sites.txt").read_text().splitlines(), 1):
        text = line.split("#", 1)[0].split()
        if text:
            claims.append((n, text))
    return claims


def mentioned(target):
    """Every address the target's hand-written files name, with where."""
    seen = {}
    for file in ("target.h", "target_hooks.c"):
        for n, line in enumerate((target.dir / file).read_text().splitlines(), 1):
            for m in LITERAL.finditer(line):
                v = int(m.group(1), 16)
                if v >= 0x10000:
                    seen.setdefault(v, "%s:%d" % (file, n))
            for m in SUB_NAME.finditer(line):
                seen.setdefault(number(m.group(1)), "%s:%d" % (file, n))
    return seen


def covered(claims):
    out = set()
    for _, words in claims:
        kind, args = words[0], words[1:]
        if kind == "insn":
            args = args[:1]
        elif kind == "other":
            args = args[1:]
        for a in args:
            try:
                out.add(number(a.replace("sub_", "")))
            except ValueError:
                pass
    return out


def check(target, xbe, kind, args, skipped, splits):
    def need_xbe():
        if xbe is None:
            skipped.append(kind)
            return False
        return True

    def func(va):
        if va not in target.functions:
            raise ValueError("0x%08X is not a lifted function" % va)
        if xbe is not None:                 # otherwise checked in gen only
            if not xbe.code(va):
                raise ValueError("0x%08X is not in a code section" % va)
            xbe.insn(va)

    if kind == "func":
        func(number(args[0]))
    elif kind == "split":
        a = number(args[0])
        func(a)
        if re.search(r"RECOMP_ABI_CALL\(0x%08Xu" % a, target.generated()):
            raise ValueError("generated code calls 0x%08X" % a)
    elif kind == "span":
        a, b = number(args[0]), number(args[1])
        func(a)
        later = [f for f in target.sorted if f > a and f not in splits]
        if not a <= b or (later and b >= later[0]):
            raise ValueError("0x%08X is not inside the function at 0x%08X (next 0x%08X)"
                             % (b, a, later[0] if later else 0))
    elif kind == "other":
        other = Target(args[0])
        if number(args[1]) not in other.functions:
            raise ValueError("0x%08X is not a lifted function of %s" % (number(args[1]), args[0]))
    elif kind == "value":
        number(args[0])
    elif kind == "ret" and xbe is None and target.hand_written(number(args[0])):
        skipped.append(kind)                # no generated body to look in
    elif kind == "ret" and xbe is None:
        call =r"PUSH32\(esp, 0x%08Xu\); RECOMP_ABI_CALL\(0x([0-9A-F]{8})u" % number(args[0])
        callees = {number(m) for m in re.findall(call, target.generated())}
        if args[1] == "*":
            if not callees and "PUSH32(esp, 0x%08Xu)" % number(args[0]) not in target.generated():
                raise ValueError("the generated code pushes no return address 0x%08X" % number(args[0]))
        elif not any(same_function(args[1], c) for c in callees):
            raise ValueError("the generated code calls %s at return 0x%08X"
                             % (", ".join("0x%08X" % c for c in sorted(callees)) or "nothing", number(args[0])))
    elif not need_xbe():
        return
    elif kind == "ret":
        i = call_ending_at(xbe, number(args[0]))
        direct, pointer = call_target(i)
        if args[1] != "*" and not same_function(args[1], direct if direct is not None else -1):
            raise ValueError("the call at 0x%08X goes to %s, not %s"
                             % (i.address, "0x%08X" % direct if direct is not None else i.op_str, args[1]))
    elif kind == "call":
        i = xbe.insn(number(args[0]))
        direct, pointer = call_target(i)
        if args[1] != "*" and not (same_function(args[1], direct if direct is not None else -1)
                                   or number(args[1]) == pointer):
            raise ValueError("the call at 0x%08X is %s, not to %s" % (i.address, i.op_str, args[1]))
    elif kind == "ref":
        i = xbe.insn(number(args[0]))
        if number(args[1]) not in operand_values(i):
            raise ValueError("0x%08X is %s %s, without 0x%08X" % (i.address, i.mnemonic, i.op_str, number(args[1])))
    elif kind == "insn":
        i = xbe.insn(number(args[0]))
        text = (i.mnemonic + " " + i.op_str).strip()
        want = " ".join(args[1:])
        if not text.startswith(want):
            raise ValueError("0x%08X is %s, not %s" % (i.address, text, want))
    elif kind == "code":
        if not xbe.code(number(args[0])):
            raise ValueError("0x%08X is not in a code section" % number(args[0]))
        xbe.insn(number(args[0]))
    elif kind == "data":
        if not xbe.section(number(args[0])):
            raise ValueError("0x%08X is not in any section" % number(args[0]))
    elif kind == "section":
        a, size = number(args[0]), number(args[1])
        if not any(s[0] == a and s[1] == size for s in xbe.sections):
            raise ValueError("no section at 0x%08X with size 0x%X" % (a, size))
        if len(args) > 2 and a + size != number(args[2]):
            raise ValueError("0x%08X + 0x%X is not 0x%08X" % (a, size, number(args[2])))
    elif kind == "entry":
        if xbe.entry != number(args[0]):
            raise ValueError("the entry point is 0x%08X" % xbe.entry)
    elif kind == "title":
        if xbe.title != number(args[0]):
            raise ValueError("the title ID is 0x%08X" % xbe.title)
    elif kind == "u32":
        got = struct.unpack("<I", xbe.read(number(args[0]), 4))[0]
        if got != number(args[1].replace("sub_", "")):
            raise ValueError("dword at 0x%08X is 0x%08X, not %s" % (number(args[0]), got, args[1]))
    elif kind == "f32":
        got = struct.unpack("<f", xbe.read(number(args[0]), 4))[0]
        if abs(got - float(args[1])) > 1e-6 * max(1.0, abs(float(args[1]))):
            raise ValueError("float at 0x%08X is %r, not %s" % (number(args[0]), got, args[1]))
    else:
        raise ValueError("unknown claim %r" % kind)


def run(name, require_xbe, uncovered_only):
    target = Target(name)
    claims = load_claims(target)
    failures = []

    missing = {v: where for v, where in mentioned(target).items() if v not in covered(claims)}
    for v, where in sorted(missing.items()):
        failures.append("0x%08X (%s) has no claim in xbe_sites.txt" % (v, where))
    if uncovered_only:
        for f in failures:
            print("  " + f)
        return not failures

    for hook in ENTER_DEF.findall((target.dir / "target_hooks.c").read_text()):
        if ("sub_%s_enter();" % hook.upper()) not in target.generated():
            failures.append("sub_%s_enter is never called by the generated code" % hook.upper())

    xbe = None
    path = target.xbe_path()
    if path.exists():
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest().upper()
        if digest != target.sha256():
            failures.append("%s has SHA-256 %s, target.h expects %s" % (path, digest, target.sha256()))
        else:
            xbe = Xbe(data)
    elif require_xbe:
        failures.append("%s not found" % path)

    skipped = []
    splits = {number(w[1]) for _, w in claims if w[0] == "split" and len(w) > 1}
    for n, words in claims:
        try:
            check(target, xbe, words[0], words[1:], skipped, splits)
        except (ValueError, IndexError) as e:
            failures.append("xbe_sites.txt:%d: %s: %s" % (n, " ".join(words), e))

    print("%s: %d claims, %d checked %s, %d skipped without the XBE, %d failed"
          % (name, len(claims), len(claims) - len(skipped),
             "against the XBE" if xbe else "in the generated code only",
             len(skipped), len(failures)))
    for f in failures:
        print("  FAIL " + f)
    return not failures


def main():
    args = sys.argv[1:]
    require_xbe = "--require-xbe" in args
    uncovered_only = "--uncovered" in args
    names = [a for a in args if not a.startswith("--")] or list(TARGETS)
    ok = all([run(n, require_xbe, uncovered_only) for n in names])
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
