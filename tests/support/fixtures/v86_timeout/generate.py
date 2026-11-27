"""Generate a PC-98 RAW 2HD image that waits for the V86 session timeout."""
from pathlib import Path
import argparse


def generate(output):
    image = bytearray(77 * 2 * 8 * 1024)
    image[:3] = bytes.fromhex("FB EB FE")  # STI, then JMP to itself
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    generate(parser.parse_args().output)
