#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class DriveState { NoDevice, Locked, Unlocked, Transient, PermissionError };

struct LunInfo {
    std::string serial;
    uint64_t size_bytes;
    bool has_children;
    std::string fs_label;
};

DriveState classify(bool usb_present, bool permission_error,
                    const std::vector<LunInfo>& luns, DriveState previous);

class BlockDevInfo {
public:
    virtual ~BlockDevInfo() = default;
    virtual std::vector<LunInfo> list_t1_luns() = 0;
};

// Implementazione concreta via GUdev: delega alla funzione libera sottostante.
class UdevBlockDevInfo : public BlockDevInfo {
public:
    std::vector<LunInfo> list_t1_luns() override;
};

// Enumerazione GUdev delle LUN Samsung T1: enumerator subsystem=block con
// match DEVTYPE=disk, filtro in C++ su ID_FS_LABEL == "T1_SETUP" oppure
// ID_MODEL con prefisso "Samsung_Portable_SSD_T1". NIENTE match su
// ID_VENDOR_ID/ID_MODEL_ID: sul disco reale il bridge USB-SATA espone il
// block device come ID_BUS=ata e tali proprietà esistono solo sul nodo USB
// padre (misura udevadm, Fix Round 1).
std::vector<LunInfo> udev_list_t1_luns();

// Nome della prima partizione (DEVTYPE=partition) figlia di un disco T1
// rilevato col filtro sopra, con prefisso /dev/. Stringa vuota se assente.
std::string udev_t1_partition_devname();  // "" se assente; es. "/dev/sda1"
