"""Regression tests for partition planning and populate helpers."""

import importlib.util
import argparse
import pathlib
import unittest
from unittest import mock


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "partition_storage.py"

spec = importlib.util.spec_from_file_location("partition_storage", MODULE_PATH)
partition_storage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(partition_storage)


class PartitionStorageTest(unittest.TestCase):
    """Exercise the dry-run planning logic without touching real disks."""

    def test_build_parted_commands_gpt_with_name(self):
        # GPT targets should receive an explicit partition label for easier
        # identification in host tools.
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
        # MSDOS does not support the same naming flow, so the helper should
        # stop after mklabel and mkpart.
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
        # Linux block devices use two partition naming schemes. The helper
        # needs to handle both without guessing wrong.
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
        # Device formatting and image formatting use different command forms,
        # so keep the device path helper separate and tested.
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
        # Image formatting needs partition offsets instead of partition paths.
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

    def test_build_create_disk_command(self):
        # The populate path shells out to the builder script in the repo.
        cmd = partition_storage.build_create_disk_command("/dev/sdb1")

        self.assertEqual(cmd[0], "python3")
        self.assertEqual(cmd[-1], "/dev/sdb1")
        self.assertTrue(cmd[1].endswith("/tools/create_disk.py"))

    @mock.patch.object(partition_storage.subprocess, "run")
    def test_populate_numos_partition_target_sets_expected_env(self, run_mock):
        # The populate helper converts partition geometry into environment
        # variables so create_disk.py can write straight into the partition.
        rc = partition_storage.populate_numos_partition_target(
            target="/dev/sdb1",
            size_sectors=65536,
            apply=True,
            offset_bytes=1048576,
            preserve_prefix=True,
        )

        self.assertEqual(rc, 0)
        run_mock.assert_called_once()
        cmd = run_mock.call_args.args[0]
        env = run_mock.call_args.kwargs["env"]
        self.assertEqual(cmd[0], "python3")
        self.assertEqual(cmd[-1], "/dev/sdb1")
        self.assertEqual(env["NUMOS_DISK_SIZE_MB"], "32")
        self.assertEqual(env["NUMOS_DISK_OFFSET_BYTES"], "1048576")
        self.assertEqual(env["NUMOS_DISK_PRESERVE_PREFIX"], "1")

    @mock.patch.object(partition_storage, "populate_numos_partition_target")
    @mock.patch.object(partition_storage, "get_partition_geometry_sectors", return_value=(2048, 67583, 65536))
    @mock.patch.object(partition_storage.subprocess, "run")
    @mock.patch.object(partition_storage, "run_command")
    @mock.patch.object(partition_storage, "assert_tooling")
    @mock.patch.object(partition_storage.os.path, "isfile", return_value=False)
    @mock.patch.object(partition_storage.os.path, "exists", return_value=True)
    def test_create_partition_populates_block_device_partition(
        self,
        _exists_mock,
        _isfile_mock,
        _assert_tooling_mock,
        run_command_mock,
        partprobe_mock,
        geometry_mock,
        populate_mock,
    ):
        # On a real block device, populate should wait for the kernel to notice
        # the new partition table, then target partition 1 directly.
        populate_mock.return_value = 0
        args = argparse.Namespace(
            target="/dev/sdb",
            table="gpt",
            fs="fat32",
            start="1MiB",
            end="100%",
            name="NUMOS",
            format=True,
            populate_numos=True,
            no_populate_numos=False,
            apply=True,
        )

        rc = partition_storage.create_partition(args)

        self.assertEqual(rc, 0)
        run_command_mock.assert_any_call(["parted", "-s", "/dev/sdb", "mklabel", "gpt"], True)
        run_command_mock.assert_any_call(
            ["parted", "-s", "/dev/sdb", "mkpart", "primary", "fat32", "1MiB", "100%"],
            True,
        )
        run_command_mock.assert_any_call(["parted", "-s", "/dev/sdb", "name", "1", "NUMOS"], True)
        run_command_mock.assert_any_call(["partprobe", "/dev/sdb"], True)
        partprobe_mock.assert_not_called()
        geometry_mock.assert_called_once_with("/dev/sdb", 1)
        populate_mock.assert_called_once_with(
            target="/dev/sdb1",
            size_sectors=65536,
            apply=True,
        )

    @mock.patch.object(partition_storage, "build_create_disk_command", return_value=["python3", "/tmp/create_disk.py", "/tmp/disk.img"])
    @mock.patch.object(partition_storage, "run_command")
    @mock.patch.object(partition_storage, "assert_tooling")
    @mock.patch.object(partition_storage.os.path, "isfile", return_value=True)
    @mock.patch.object(partition_storage.os.path, "exists", return_value=True)
    def test_create_partition_image_dry_run_with_populate_skips_geometry_lookup(
        self,
        _exists_mock,
        _isfile_mock,
        _assert_tooling_mock,
        run_command_mock,
        _build_create_disk_command_mock,
    ):
        # Dry run mode should never inspect geometry that only exists after the
        # partition table has been written.
        args = argparse.Namespace(
            target="/tmp/disk.img",
            table="gpt",
            fs="fat32",
            start="1MiB",
            end="100%",
            name="NUMOS",
            format=True,
            populate_numos=True,
            no_populate_numos=False,
            apply=False,
        )

        with mock.patch.object(partition_storage, "get_partition_geometry_sectors") as geometry_mock:
            rc = partition_storage.create_partition(args)

        self.assertEqual(rc, 0)
        geometry_mock.assert_not_called()
        run_command_mock.assert_any_call(["parted", "-s", "/tmp/disk.img", "mklabel", "gpt"], False)
        run_command_mock.assert_any_call(
            ["parted", "-s", "/tmp/disk.img", "mkpart", "primary", "fat32", "1MiB", "100%"],
            False,
        )


if __name__ == "__main__":
    unittest.main()
