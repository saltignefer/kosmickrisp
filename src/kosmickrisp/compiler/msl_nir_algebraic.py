# Copyright 2025 LunarG, Inc.
# Copyright 2025 Google LLC
# Copyright 2022 Alyssa Rosenzweig
# Copyright 2021 Collabora, Ltd.
# Copyright 2016 Intel Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys
import math

a = 'a'
b = 'b'

lower = [
  # Based on the VIR lowering
  (('f2f16_rtz', 'a@32'),
   ('bcsel', ('flt', ('fabs', a), ('fabs', ('f2f32', ('f2f16_rtne', a)))),
    ('isub', ('f2f16_rtne', a), 1), ('f2f16_rtne', a))),

  # Handle imod behavior with negative values using lowering
  (('imod', a, b),
   ('bcsel', ('ior', ('ieq', ('irem', a, b), 0), ('ieq', ('isign', a), ('isign', b))),
    ('irem', a, b), ('iadd', ('irem', a, b), b))),
]

# msl_nir_lower_fp64 runs after the optimization loop that lowers these
lower += [
  (('unpack_64_2x32_split_x', a), ('u2u32', a)),
  (('unpack_64_2x32_split_y', a), ('u2u32', ('ushr', a, 32))),
  (('pack_64_2x32_split', a, b),
   ('ior', ('u2u64', a), ('ishl', ('u2u64', b), 32))),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()

def run():
    import nir_algebraic  # pylint: disable=import-error

    print('#include "msl_private.h"')

    print(nir_algebraic.AlgebraicPass("msl_nir_lower_algebraic_late", lower).render())

if __name__ == '__main__':
    main()
