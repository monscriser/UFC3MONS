"""Add the Reborn 1.1 jump target confirmed by the issue #24 minidump."""

import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    anchor = "\t\tif (ctx.ctr.u32 == 0x82B50764) goto loc_82B50764;\n"
    start = source.index("DEFINE_REX_FUNC(sub_82B50588)")
    end = source.index("\nDEFINE_REX_FUNC(", start + 1)
    function = source[start:end]
    if function.count(anchor) != 1 or "0x83669280" in function:
        parser.error("Expected the previously patched Reborn jump table")
    updated = function.replace(
        anchor,
        anchor + "\t\t// Reborn 1.1 uses a separate handler for table entry 7.\n"
        "\t\tif (ctx.ctr.u32 == 0x83669280) {\n"
        "\t\t\tsub_83669280(ctx, base);\n"
        "\t\t\treturn;\n"
        "\t\t}\n",
    )
    declaration = "DECLARE_REX_FUNC(sub_83669280);\n\n"
    args.output.write_text(source[:start] + declaration + updated + source[end:], encoding="utf-8")


if __name__ == "__main__":
    main()
