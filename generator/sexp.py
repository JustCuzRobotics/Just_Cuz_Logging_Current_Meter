#!/usr/bin/env python3
"""Minimal s-expression reader/writer for KiCad files.

Used by both the generator (to pretty-print) and the verifier (to read the finished
file back without trusting the generator's in-memory state).
"""

import re as _re


class Sym(str):
    """A bare token -- printed without quotes."""
    __slots__ = ()


def dumps(node, indent=0, inline=False):
    """Serialise a nested list to KiCad-style s-expression text."""
    pad = "  " * indent
    if not isinstance(node, list):
        return _atom(node)

    head = node[0] if node else Sym("")
    parts = [_atom(head)]
    simple = all(not isinstance(c, list) for c in node[1:])

    if inline or simple:
        parts += [_atom(c) for c in node[1:]]
        return f"{pad}(" + " ".join(parts) + ")" if not inline else \
            "(" + " ".join(parts) + ")"

    out = [f"{pad}({_atom(head)}"]
    buf = []
    for c in node[1:]:
        if isinstance(c, list):
            if buf:
                out[-1] += " " + " ".join(buf)
                buf = []
            out.append(dumps(c, indent + 1))
        else:
            buf.append(_atom(c))
    if buf:
        out[-1] += " " + " ".join(buf)
    out.append(f"{pad})")
    return "\n".join(out)


def _atom(a):
    if isinstance(a, Sym):
        return str(a)
    if isinstance(a, bool):
        return "yes" if a else "no"
    if isinstance(a, float):
        # repr() gives the shortest string that round-trips exactly. Fixed-precision
        # formatting silently truncated vendor footprint coordinates such as
        # 0.800181102362 (an imported 0.0315" dimension), which broke round-trip checks.
        if a == int(a) and abs(a) < 1e15:
            return str(int(a))
        return repr(a)
    if isinstance(a, int):
        return str(a)
    s = str(a).replace("\\", "\\\\").replace('"', '\\"')
    return f'"{s}"'


_INT_RE = _re.compile(r"^[+-]?\d+$")
_FLOAT_RE = _re.compile(r"^[+-]?(\d+\.\d*|\.\d+|\d+)([eE][+-]?\d+)?$")


def _num(tok):
    """Convert a bare token to int/float, or keep it as a Sym.

    Deliberately regex-gated rather than using a try/except around int()/float().
    Python accepts underscores as digit separators (PEP 515), so int("4_1") == 41 —
    which silently corrupts KiCad pad names like `4_1` and `5_16` into numbers.
    That bug is not hypothetical; it mangled a real ACS770 footprint.
    """
    if _INT_RE.match(tok):
        return int(tok)
    if _FLOAT_RE.match(tok):
        return float(tok)
    return Sym(tok)


def loads(text):
    """Parse s-expression text into nested lists. Strings stay str, tokens become Sym."""
    i, n = 0, len(text)
    stack, cur = [], None

    def skip_ws(i):
        while i < n:
            if text[i] in " \t\r\n":
                i += 1
            elif text[i] == ";":
                while i < n and text[i] != "\n":
                    i += 1
            else:
                break
        return i

    while True:
        i = skip_ws(i)
        if i >= n:
            break
        c = text[i]
        if c == "(":
            new = []
            if cur is not None:
                cur.append(new)
                stack.append(cur)
            cur = new
            i += 1
        elif c == ")":
            if stack:
                cur = stack.pop()
            else:
                root = cur
                cur = None
                i += 1
                i = skip_ws(i)
                if i >= n:
                    return root
                continue
            i += 1
        elif c == '"':
            i += 1
            buf = []
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    i += 1
                buf.append(text[i])
                i += 1
            i += 1
            cur.append("".join(buf))
        else:
            j = i
            while j < n and text[j] not in ' \t\r\n()"':
                j += 1
            tok = text[i:j]
            cur.append(_num(tok))
            i = j
    return cur


def find(node, tag):
    """First direct child list whose head is `tag`."""
    for c in node[1:] if node else []:
        if isinstance(c, list) and c and c[0] == tag:
            return c
    return None


def findall(node, tag):
    return [c for c in (node[1:] if node else [])
            if isinstance(c, list) and c and c[0] == tag]


def val(node, tag, idx=1, default=None):
    c = find(node, tag)
    return c[idx] if c and len(c) > idx else default
