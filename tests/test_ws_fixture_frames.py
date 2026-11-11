"""Offline WebSocket SERVER-fixture framing; not the production client."""
import unittest
import random
from typing import Any, cast
from ws_fixture_frames import MAX_PAYLOAD, server_frame, take_client_frame


def masked_frame(payload, opcode=1, final=True):
    """Independent test-side client wire construction, never runtime code."""
    n = len(payload)
    first = (128 if final else 0) | opcode
    if n < 126:
        head = bytes((first, 128 | n))
    elif n <= 65535:
        head = bytes((first, 254)) + n.to_bytes(2, "big")
    else:
        head = bytes((first, 255)) + n.to_bytes(8, "big")
    key = b"abcd"
    return head + key + bytes(c ^ key[i % 4] for i, c in enumerate(payload))


class FrameTests(unittest.TestCase):
    def test_server_text_frame(self):
        self.assertEqual(server_frame(b"{}", opcode=1, final=True), b"\x81\x02{}")

    def test_masked_client_frame(self):
        key = b"abcd"
        payload = b"{}"
        wire = b"\x81\x82" + key + bytes(c ^ key[i % 4] for i, c in enumerate(payload))
        buf = bytearray(wire + b"tail")
        self.assertEqual(take_client_frame(buf), (1, True, payload))
        self.assertEqual(buf, b"tail")


    def test_length_encodings_and_partial_preservation(self):
        for n in (0, 1, 125, 126, 65535, 65536, MAX_PAYLOAD):
            with self.subTest(length=n):
                payload = b"x" * n
                if n < 126:
                    header = bytes((129, n))
                elif n <= 65535:
                    header = bytes((129, 126)) + n.to_bytes(2, "big")
                else:
                    header = bytes((129, 127)) + n.to_bytes(8, "big")
                self.assertEqual(server_frame(payload), header + payload)
                wire = masked_frame(payload)
                cuts = range(len(wire)) if n < 126 else sorted({0, 1, 2, 3, 9, 10, len(wire)-1})
                for cut in cuts:
                    buf = bytearray(wire[:cut])
                    before = bytes(buf)
                    self.assertIsNone(take_client_frame(buf))
                    self.assertEqual(buf, before)
                    buf.extend(wire[cut:])
                    self.assertEqual(take_client_frame(buf), (1, True, payload))
                    self.assertEqual(buf, b"")

    def test_coalesced_and_fragment_frames(self):
        buf = bytearray(masked_frame(b"ab", opcode=1, final=False) + masked_frame(b"cd", opcode=0))
        self.assertEqual(take_client_frame(buf), (1, False, b"ab"))
        self.assertEqual(take_client_frame(buf), (0, True, b"cd"))
        self.assertEqual(buf, b"")
        for opcode in (2, 8, 9, 10):
            self.assertEqual(take_client_frame(bytearray(masked_frame(b"hi", opcode))), (opcode, True, b"hi"))

    def test_malformed_or_oversized_prefixes_leave_buffer_intact(self):
        bad = [b"\x81\x00", b"\xc1\x80abcd", b"\x83\x80abcd", b"\x09\x80abcd",
               b"\x89\xfe", b"\x81\xfe\x00\x01",
               b"\x81\xff" + (1 << 63).to_bytes(8, "big"),
               b"\x81\xff" + (65535).to_bytes(8, "big"),
               b"\x81\xff" + (MAX_PAYLOAD+1).to_bytes(8, "big")]
        for wire in bad:
            with self.subTest(header=wire.hex()):
                buf = bytearray(wire)
                with self.assertRaises(ValueError):
                    take_client_frame(buf)
                self.assertEqual(buf, wire)
        buf = bytearray(b"\x81\x85")
        with self.assertRaises(ValueError):
            take_client_frame(buf, maximum=4)
        self.assertEqual(buf, b"\x81\x85")

    def test_server_policy_and_bad_types(self):
        for payload, opcode, final in [(b"x", 3, True), (b"x", True, True), (b"x", 1, 1),
                                      (b"x", 9, False), (b"x"*126, 9, True),
                                      (b"x"*(MAX_PAYLOAD+1), 1, True), (bytearray(), 1, True)]:
            with self.assertRaises(ValueError):
                server_frame(payload, opcode=opcode, final=final)
        for maximum in (-1, MAX_PAYLOAD+1, True, "1"):
            buf = bytearray(masked_frame(b""))
            before = bytes(buf)
            with self.assertRaises(ValueError):
                take_client_frame(buf, maximum=cast(Any, maximum))
            self.assertEqual(buf, before)
        with self.assertRaises(ValueError):
            take_client_frame(cast(Any, b""))
        self.assertEqual(take_client_frame(bytearray(masked_frame(b"")), maximum=0), (1, True, b""))

    def test_deterministic_mask_properties(self):
        rng = random.Random(12)
        for _ in range(500):
            payload = bytes(rng.randrange(256) for _ in range(rng.randrange(512)))
            buf = bytearray(masked_frame(payload) + b"tail")
            self.assertEqual(take_client_frame(buf), (1, True, payload))
            self.assertEqual(buf, b"tail")


if __name__ == "__main__":
    unittest.main()
