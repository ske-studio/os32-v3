"""Compatibility entry point for the GF5 ROM host case."""
import sys
import test_gf5


if __name__ == '__main__':
    sys.argv.extend(['--case', 'rom'])
    raise SystemExit(test_gf5.main())
