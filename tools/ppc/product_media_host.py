"""Check rejection of truncated and overlong install partitions before publication."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import product_media


def exercise(parser, whole, volume, entry):
    assert parser(whole) == volume
    sectors = len(volume)//512
    short = bytearray(whole)
    long = bytearray(whole) + bytes(512)
    struct.pack_into('>I', short, entry+12, sectors-1)
    struct.pack_into('>I', long, entry+12, sectors+1)
    oversized = long[:-512]
    for invalid in (oversized, whole[:-1], long, short):
        try:
            parser(invalid)
        except ValueError:
            continue
        raise AssertionError('incorrect HFS partition length accepted')


def main():
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        src = tmp/'cd'
        src.mkdir()
        (src/'FILE').write_bytes(b'host fixture')
        image = tmp/'hybrid.iso'
        subprocess.run(['genisoimage', '-quiet', '-hfs', '-part', '-no-desktop',
                        '-o', image, src], check=True)
        data = image.read_bytes()
        volume = product_media.partition(data)
        entry = data.index(b'Apple_HFS')-48
        start, = struct.unpack_from('>I', data, entry+8)
        whole = data[:start*512+len(volume)]
        exercise(product_media.partition, whole, volume, entry)
        if '--mutate' in sys.argv:
            source = Path(product_media.__file__).read_text()
            # Each mutant must admit a malformed fixture through the production parser.
            for before, after in [('first*512 + blocks*block_size != len(volume)', 'False'),
                                  ('len(volume) != sectors*512', 'False')]:
                assert source.count(before) == 1
                namespace = {'__name__': 'mutated_product_media'}
                exec(compile(source.replace(before, after), str(image), 'exec'), namespace)
                try:
                    exercise(namespace['partition'], whole, volume, entry)
                except AssertionError as error:
                    assert str(error) == 'incorrect HFS partition length accepted'
                else:
                    raise AssertionError('mutation not observable: '+before)
                print(before, 'RED')
    print('PASS classic HFS partition bounds')


if __name__ == '__main__':
    main()
