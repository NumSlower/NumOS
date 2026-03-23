import importlib.util
import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "partition_storage.py"

spec = importlib.util.spec_from_file_location("partition_storage", MODULE_PATH)
partition_storage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(partition_storage)


class PartitionStorageTest(unittest.TestCase):
    def test_build_parted_commands_gpt_with_name(self):
        cmds = partition_storage.build_parted_commands(
            target="/tmp/disk.img",
            table="gpt",
            fs="fat32",
            start="1MiB",
            end="100%",
            name="NUMOS",
        )
        self.assertEqual(cmds[0], ["parted", "-s", "/tmp/disk.img", "mklabel", "gpt"])
        self.assertEqual(
            cmds[1],
            ["parted", "-s", "/tmp/disk.img", "mkpart", "primary", "fat32", "1MiB", "100%"],
        )
        self.assertEqual(cmds[2], ["parted", "-s", "/tmp/disk.img", "name", "1", "NUMOS"])

    def test_build_parted_commands_msdos_without_name(self):
        cmds = partition_storage.build_parted_commands(
            target="/dev/sdb",
            table="msdos",
            fs="none",
            start="1MiB",
            end="100%",
            name="IGNORED",
        )
        self.assertEqual(cmds[0], ["parted", "-s", "/dev/sdb", "mklabel", "msdos"])
        self.assertEqual(
            cmds[1],
            ["parted", "-s", "/dev/sdb", "mkpart", "primary", "1MiB", "100%"],
        )
        self.assertEqual(len(cmds), 2)

    def test_get_partition_path_for_device(self):
        self.assertEqual(partition_storage.get_partition_path_for_device("/dev/sdb", 1), "/dev/sdb1")
        self.assertEqual(
            partition_storage.get_partition_path_for_device("/dev/nvme0n1", 1),
            "/dev/nvme0n1p1",
        )
        self.assertEqual(
            partition_storage.get_partition_path_for_device("/dev/mmcblk0", 1),
            "/dev/mmcblk0p1",
        )

    def test_build_mkfs_command(self):
        self.assertEqual(
            partition_storage.build_mkfs_command("/dev/sdb1", "fat32"),
            ["mkfs.vfat", "-F", "32", "/dev/sdb1"],
        )
        self.assertEqual(
            partition_storage.build_mkfs_command("/dev/sdb1", "ext4"),
            ["mkfs.ext4", "-F", "/dev/sdb1"],
        )
        self.assertIsNone(partition_storage.build_mkfs_command("/dev/sdb1", "none"))

    def test_build_mkfs_file_command(self):
        self.assertEqual(
            partition_storage.build_mkfs_file_command("/tmp/disk.img", "fat32", 2048),
            ["mkfs.vfat", "-F", "32", "--offset=2048", "/tmp/disk.img"],
        )
        self.assertEqual(
            partition_storage.build_mkfs_file_command("/tmp/disk.img", "ext4", 2048),
            ["mkfs.ext4", "-F", "-E", "offset=1048576", "/tmp/disk.img"],
        )
        self.assertIsNone(
            partition_storage.build_mkfs_file_command("/tmp/disk.img", "none", 2048)
        )


if __name__ == "__main__":
    unittest.main()
