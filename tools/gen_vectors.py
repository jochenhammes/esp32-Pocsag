#!/usr/bin/env python3
"""Erzeugt test/test_pocsag/vectors.h mit Referenz-Bitströmen aus pluto-tx (pocsag_codec.build_transmission).

Aufruf:  python3 tools/gen_vectors.py [/pfad/zu/plutosdr]
pluto-tx wird nur gelesen (das Modul wird direkt per Dateipfad geladen, es entsteht kein __pycache__).
"""
import importlib.util
import pathlib
import sys

PLUTO_DIR = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "~/Dokumente/plutosdr").expanduser()
spec = importlib.util.spec_from_file_location("pocsag_codec", PLUTO_DIR / "pluto_tx" / "pocsag_codec.py")
c = importlib.util.module_from_spec(spec)
sys.dont_write_bytecode = True
spec.loader.exec_module(c)

# (name, ric, function, kind, charset, text)
CASES = [
    ("numeric", 1234567, 0, "numeric", "ascii", "0123456789*U -]["),
    ("alpha", 1234, 3, "alpha", "ascii", "Hello DAPNET"),
    ("umlaut", 2504, 3, "alpha", "de", "Grüße ÄÖÜäöüß"),
    ("frame7", 7, 2, "alpha", "ascii", "Frame 7 at the end of the batch"),
    ("ric_max", 0x1FFFFF, 1, "alpha", "ascii", "Max RIC"),
    ("long", 4513, 3, "alpha", "ascii", "".join(chr(0x20 + (i * 7) % 95) for i in range(300))),
    ("tone", 99, 1, "tone", "ascii", ""),
]


def c_str(s):
    return '"' + "".join(f"\\x{b:02x}" for b in s.encode("utf-8")) + '"'


def main():
    out = ["// GENERIERT von tools/gen_vectors.py -- nicht von Hand ändern.", "#pragma once", "#include <stdint.h>", ""]
    names = []
    for name, ric, func, kind, charset, text in CASES:
        payload = None if kind == "tone" else c.text_to_bits(text, kind, charset)
        bits = c.build_transmission(ric, func, payload)
        data = bytearray((len(bits) + 7) // 8)
        for i, b in enumerate(bits):
            data[i >> 3] |= b << (7 - (i & 7))
        out.append(f"static const uint8_t vec_{name}_bits[] = {{" + ",".join(f"0x{x:02x}" for x in data) + "};")
        names.append((name, ric, func, kind, charset, len(bits), text))
    out += ["", "struct Vector {", "  const char *name;", "  const uint8_t *bits;", "  uint32_t nbits;", "  uint32_t ric;",
            "  uint8_t function;", "  const char *kind;     // numeric | alpha | tone", "  const char *charset;  // ascii | de",
            "  const char *text;     // erwarteter Text (UTF-8)", "};", "", "static const Vector VECTORS[] = {"]
    for name, ric, func, kind, charset, nbits, text in names:
        out.append(f'  {{"{name}", vec_{name}_bits, {nbits}, {ric}u, {func}, "{kind}", "{charset}", {c_str(text)}}},')
    out += ["};", "static const int N_VECTORS = sizeof(VECTORS) / sizeof(VECTORS[0]);", ""]
    # Einzelne Codewörter als Referenz für BCH/Parität
    cws = [
        ("address(0x2AAAA, 2)", c.address_codeword(0x2AAAA, 2)),
        ("address(1, 0)", c.address_codeword(1, 0)),
        ("address(0x3FFFF, 3)", c.address_codeword(0x3FFFF, 3)),
        ("message(0xABCDE)", c.message_codeword(0xABCDE)),
        ("message(0)", c.message_codeword(0)),
        ("message(0xFFFFF)", c.message_codeword(0xFFFFF)),
    ]
    out.append("struct CodewordRef { uint32_t cw; uint8_t isAddress; uint32_t field; uint8_t function; };")
    out.append("static const CodewordRef CODEWORDS[] = {")
    for (label, cw), (isaddr, field, func) in zip(cws, [(1, 0x2AAAA, 2), (1, 1, 0), (1, 0x3FFFF, 3),
                                                       (0, 0xABCDE, 0), (0, 0, 0), (0, 0xFFFFF, 0)]):
        out.append(f"  {{0x{cw:08X}u, {isaddr}, 0x{field:X}u, {func}}},  // {label}")
    out += ["};", "static const int N_CODEWORDS = sizeof(CODEWORDS) / sizeof(CODEWORDS[0]);", ""]
    path = pathlib.Path(__file__).resolve().parent.parent / "test" / "test_pocsag" / "vectors.h"
    path.write_text("\n".join(out))
    print("geschrieben:", path, f"({len(CASES)} Vektoren)")


main()
