"""Regression tests for the host-side Numloss codec helpers."""

import importlib.util
import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLS_DIR = REPO_ROOT / "tools"


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


numloss_codec = load_module("numloss_codec", TOOLS_DIR / "numloss_codec.py")
create_disk = load_module("create_disk", TOOLS_DIR / "create_disk.py")


class NumlossCodecTest(unittest.TestCase):
    """Keep the host-side archive format stable for build tooling."""

    def test_round_trip_text_and_binary_payload(self):
        payload = (
            b"AAAAABBBBBCCCCCDDDDDEEEEE"
            + bytes(range(64))
            + (b"\x00\x00\x00\x00\xFF\xFF\xFF\xFF" * 32)
        )

        archive = numloss_codec.encode(payload)
        restored = numloss_codec.decode(archive)

        self.assertTrue(numloss_codec.is_archive(archive))
        self.assertEqual(restored, payload)

    def test_chunked_round_trip_for_large_payload(self):
        payload = (bytes(range(256)) * 2048) + (b"A" * 131072)

        archive = numloss_codec.encode(payload, chunk_size=131072)
        restored = numloss_codec.decode(archive)

        self.assertTrue(numloss_codec.is_archive(archive))
        self.assertEqual(restored, payload)
        self.assertEqual(archive[4], numloss_codec.VERSION_V2)

    def test_v3_transform_selected_for_word_structured_payload(self):
        payload = b"".join((value.to_bytes(4, "little") for value in range(16384)))

        archive = numloss_codec.encode(payload)
        restored = numloss_codec.decode(archive)
        word_delta_archive = numloss_codec._encode_v3_with_transform(
            payload,
            numloss_codec.TRANSFORM_DELTA32LE,
        )
        group_delta_archive = numloss_codec._encode_v3_with_transform(
            payload,
            numloss_codec.TRANSFORM_GROUP4_DELTA8,
        )

        self.assertEqual(restored, payload)
        self.assertEqual(archive[4], numloss_codec.VERSION_V3)
        self.assertEqual(archive[5], numloss_codec.TRANSFORM_DELTA32LE)
        self.assertLess(len(word_delta_archive), len(group_delta_archive))
        self.assertEqual(len(archive), len(word_delta_archive))
        self.assertLess(len(archive), len(payload) // 10)

    def test_new_match_format_beats_legacy_ratio_on_repo_text(self):
        payload = (REPO_ROOT / "README.md").read_bytes()

        archive = numloss_codec.encode(payload)

        self.assertLess(len(archive), 4400)

    def test_text_dictionary_transform_improves_requested_benchmark(self):
        payload = (REPO_ROOT / "user" / "files" / "home" / "Test.txt").read_bytes()

        archive = numloss_codec.encode(payload)
        restored = numloss_codec.decode(archive)

        self.assertEqual(restored, payload)
        self.assertEqual(archive[4], numloss_codec.VERSION_V4)
        self.assertEqual(archive[5], numloss_codec.TRANSFORM_TEXT_PROSE)
        self.assertLess(len(archive), 5820)

    def test_maybe_pack_record_packs_useful_elf_payload(self):
        record = create_disk.create_file_record("demo.elf", b"\x90" * 4096)

        packed = create_disk.maybe_pack_record(record)

        self.assertIsNot(packed, record)
        self.assertTrue(packed["packed"])
        self.assertLess(packed["size"], record["size"])
        self.assertEqual(numloss_codec.decode(packed["data"]), record["data"])

    def test_maybe_pack_record_keeps_small_unhelpful_payload_raw(self):
        record = create_disk.create_file_record("tiny.elf", b"abcdefg")

        packed = create_disk.maybe_pack_record(record)

        self.assertIs(packed, record)

    def test_maybe_pack_record_ignores_non_elf_records(self):
        record = create_disk.create_file_record("notes.txt", b"A" * 4096)

        packed = create_disk.maybe_pack_record(record)

        self.assertIs(packed, record)


if __name__ == "__main__":
    unittest.main()
