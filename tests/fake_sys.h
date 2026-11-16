#pragma once
// Fake /sys laid out like a real kernel for one NVMe disk (nvme0 / nvme0n1 / p2).
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

struct FakeSys {
    std::filesystem::path root;
    FakeSys() {
        namespace fs = std::filesystem;
        root = fs::temp_directory_path() / ("status-sys-" + std::to_string(::getpid()) + "-" + std::to_string(rand()));
        fs::path ctrl = root / "devices/pci0000:00/0000:00:01.0/nvme/nvme0";
        fs::path disk = ctrl / "nvme0n1";
        fs::create_directories(disk / "nvme0n1p2");
        std::ofstream(disk / "nvme0n1p2/partition") << "2\n";
        fs::create_directories(ctrl / "hwmon3");
        std::ofstream(ctrl / "hwmon3/temp1_input") << "48850\n";
        fs::create_directory_symlink(ctrl, disk / "device");
        fs::create_directories(root / "dev/block");
        fs::create_directory_symlink(disk / "nvme0n1p2", root / "dev/block/259:2");
        fs::create_directories(root / "class/block");
        fs::create_directory_symlink(disk / "nvme0n1p2", root / "class/block/nvme0n1p2");
        fs::create_directory_symlink(disk, root / "class/block/nvme0n1");
        fs::create_directories(root / "class/nvme");
        fs::create_directory_symlink(ctrl, root / "class/nvme/nvme0");
        // LVM on top of nvme0n1p2: dm-0 with a single slave.
        fs::path dm = root / "devices/virtual/block/dm-0";
        fs::create_directories(dm / "slaves");
        fs::create_directory_symlink(disk / "nvme0n1p2", dm / "slaves/nvme0n1p2");
        fs::create_directory_symlink(dm, root / "dev/block/253:0");
        fs::create_directory_symlink(dm, root / "class/block/dm-0");
    }
    ~FakeSys() { std::filesystem::remove_all(root); }
};
