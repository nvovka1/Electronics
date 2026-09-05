"""Regenerate docs/log_dict.csv from the LogCode enum.

The dictionary is a release artifact: it ships next to the .bin so that a log
dumped from a node in the field can be decoded months later. Generating it from
the enum is what stops the two drifting apart - a hand-maintained copy is wrong
by the third release, and then an old dump decodes to the wrong story.

    python scripts/gen_log_dict.py
"""

import io
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "src", "core", "log.h")
TARGET = os.path.join(ROOT, "docs", "log_dict.csv")


def quote(value):
    if "," in value or '"' in value:
        return '"%s"' % value.replace('"', '""')
    return value


def main():
    text = io.open(SOURCE, encoding="utf-8").read()
    body = text.split("enum LogCode : uint8_t {", 1)[1].split("};", 1)[0]

    rows = []
    for line in body.splitlines():
        match = re.match(r"\s*(E_[A-Z0-9_]+)\s*=\s*(\d+)\s*,\s*(?://\s*(.*))?$", line)
        if not match:
            continue

        name, code, comment = match.group(1), int(match.group(2)), (match.group(3) or "").strip()

        # "arg = X; note" splits into the argument's meaning and everything else.
        arg, note = "", comment
        if comment.startswith("arg ="):
            parts = comment[len("arg =") :].split(";", 1)
            arg = parts[0].strip()
            note = parts[1].strip() if len(parts) > 1 else ""

        rows.append((code, name[2:].lower(), arg, note))

    rows.sort()

    out = io.StringIO()
    out.write("code,name,arg_meaning,notes\n")
    for code, name, arg, note in rows:
        out.write("%d,%s,%s,%s\n" % (code, name, quote(arg), quote(note)))

    io.open(TARGET, "w", encoding="utf-8", newline="\n").write(out.getvalue())
    print("wrote %s (%d codes)" % (os.path.relpath(TARGET, ROOT), len(rows)))


if __name__ == "__main__":
    main()
