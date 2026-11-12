"""Run the far-plane variant of libgxr_quad_native.so through the host interpreter.

  python -B -I -m unittest discover -s tests -p "test_*.py"
      (NATIVE_QUAD_FAR_HELPER=<far build>; NATIVE_QUAD_HELPER=<1.2 build> for the bit-for-bit and
       scaling comparisons)

The variant is the same source compiled with -DGXR_SQ_DISTANCE_SLOT. Its writable slot holds one
little-endian float32 at +0x1c50; every test rewrites those four bytes in a temporary copy so the
tracked payload is never modified. Host evidence only: not Bionic, not a headset.
"""
import os
import shutil
import struct
import tempfile
import unittest

from test_helper import GOLDEN, HEAP, META, Harness

HERE = os.path.dirname(os.path.abspath(__file__))
FAR = os.environ.get('NATIVE_QUAD_FAR_HELPER')
STOCK = os.environ.get('NATIVE_QUAD_HELPER')
SLOT = 0x1C50
MARKER = b'GXRQUADDIST16M0'
BUILD_ID = b'GXR-NATIVE-QUAD-1.2-FAR-ABI1'
STOCK_DISTANCE = 1.996000051498413


def _require(path):
    return path and os.path.exists(path)


def run_quad_case(harness, state, case):
    """Same staging as test_helper.HelperTests.run_quad_case, usable with any loaded helper."""
    mem = harness.cpu.mem
    mem.write(META, b'\0' * 0x100)
    mem.write(META + 4, struct.pack('<12f', *case['matrix']))
    views = HEAP + 0x220000
    mem.write(views, b'\0' * 0x80)
    for e in range(2):
        mem.write(views + e * 0x40 + 0x10, struct.pack('<7f', *case['eye']))
    harness.call('EYES', state, views)
    harness.fov_value = case['fov']
    for e in range(2):
        mem.write(META + 0xC8 + e * 8, struct.pack('<4h', 100, 100, 800, 700))
        harness.call('FRAME_EYE', state, META, e, 2048, 2048)
    harness.call('FRAME', state, META)
    return harness.quad(state, 0)


class FarHelperTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.mkdtemp(prefix='gxr-far-helper-')
        self.addCleanup(shutil.rmtree, self.scratch, True)

    def variant(self, distance):
        data = bytearray(open(FAR, 'rb').read())
        self.assertEqual(data[SLOT - 16:SLOT - 1], MARKER)
        self.assertEqual(data[SLOT - 1], 0)
        struct.pack_into('<f', data, SLOT, distance)
        path = os.path.join(self.scratch, 'far-%s.so' % distance)
        with open(path, 'wb') as handle:
            handle.write(data)
        return path

    def quad(self, helper, case):
        harness = Harness(helper)
        state = harness.create()
        self.assertNotEqual(state, 0)
        return harness, run_quad_case(harness, state, case)

    @unittest.skipUnless(_require(FAR), 'set NATIVE_QUAD_FAR_HELPER to the far build')
    def test_tracked_payload_layout_and_identity(self):
        data = open(FAR, 'rb').read()
        self.assertEqual(data.find(MARKER), data.rfind(MARKER), 'marker must be unique')
        self.assertEqual(data[SLOT - 16:SLOT - 1], MARKER)
        self.assertEqual(data[SLOT - 1], 0, 'marker must be NUL-terminated')
        self.assertEqual(data[SLOT:SLOT + 4], struct.pack('<f', STOCK_DISTANCE))
        self.assertIn(BUILD_ID, data)

    @unittest.skipUnless(_require(FAR) and _require(STOCK), 'set NATIVE_QUAD_FAR_HELPER and NATIVE_QUAD_HELPER')
    def test_default_slot_matches_12_geometry_bit_for_bit(self):
        stock = Harness(STOCK)
        near = Harness(self.variant(STOCK_DISTANCE))
        for case in GOLDEN['quad']['cases']:
            s = stock.create()
            expected = run_quad_case(stock, s, case)
            t = near.create()
            actual = run_quad_case(near, t, case)
            self.assertEqual(expected[0x48:0x70], actual[0x48:0x70], case)

    @unittest.skipUnless(_require(FAR) and _require(STOCK), 'set NATIVE_QUAD_FAR_HELPER and NATIVE_QUAD_HELPER')
    def test_orientation_matches_and_position_size_scale_by_distance(self):
        # Only the quad's own centre offset depends on d. Zero the head and eye translations of the
        # golden inputs so the composed position is exactly the scaled local offset.
        for case in GOLDEN['quad']['cases'][::10]:
            matrix = list(case['matrix'])
            matrix[3] = matrix[7] = matrix[11] = 0.0
            eye = list(case['eye'])
            eye[4] = eye[5] = eye[6] = 0.0
            centred = dict(case, matrix=matrix, eye=eye)
            stock, expected = self.quad(STOCK, centred)
            self.assertNotEqual(stock, None)
            for distance in (16, 64, 250):
                harness, actual = self.quad(self.variant(distance), centred)
                self.assertEqual(expected[0x48:0x58], actual[0x48:0x58], 'orientation is distance independent')
                ratio = distance / STOCK_DISTANCE
                expected_position = struct.unpack_from('<3f', expected, 0x58)
                actual_position = struct.unpack_from('<3f', actual, 0x58)
                for a, b in zip(actual_position, expected_position):
                    self.assertLessEqual(abs(a - b * ratio), 1e-5 * max(1.0, abs(b * ratio)),
                                         (distance, actual_position))
                expected_size = struct.unpack_from('<2f', expected, 0x64)
                actual_size = struct.unpack_from('<2f', actual, 0x64)
                for a, b in zip(actual_size, expected_size):
                    self.assertLessEqual(abs(a - b * ratio), 1e-5 * max(1.0, abs(b * ratio)),
                                         (distance, actual_size))
                self.assertNotEqual(struct.unpack_from('<2f', actual, 0x64), (0.0, 0.0))

    @unittest.skipUnless(_require(FAR), 'set NATIVE_QUAD_FAR_HELPER to the far build')
    def test_out_of_range_slot_makes_create_return_zero(self):
        for bad in (0.5, 0.0, 1001.0, 1.0e9, float('inf'), float('nan'), -16.0):
            harness = Harness(self.variant(bad))
            self.assertEqual(harness.create(), 0, bad)

    @unittest.skipUnless(_require(FAR), 'set NATIVE_QUAD_FAR_HELPER to the far build')
    def test_range_edges_are_accepted(self):
        for good in (1.0, 1000.0):
            harness = Harness(self.variant(good))
            self.assertNotEqual(harness.create(), 0, good)

    @unittest.skipUnless(_require(FAR), 'set NATIVE_QUAD_FAR_HELPER to the far build')
    def test_option_distances_create_and_move_geometry(self):
        case = GOLDEN['quad']['cases'][0]
        sizes = {}
        for distance in (16, 64, 250):
            harness, quad = self.quad(self.variant(distance), case)
            sizes[distance] = struct.unpack_from('<2f', quad, 0x64)
            self.assertGreater(sizes[distance][0], 0.0)
        self.assertLess(sizes[16][0], sizes[64][0])
        self.assertLess(sizes[64][0], sizes[250][0])


if __name__ == '__main__':
    unittest.main()
