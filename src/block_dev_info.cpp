#include "block_dev_info.hpp"

#include <gudev/gudev.h>

#include <cctype>
#include <cstring>

static constexpr uint64_t kMaxVirtDisk = 1ull * 1024ull * 1024ull * 1024ull;  // 1 GiB

// Firma T1 da misura reale (udevadm info --query=property su /dev/sda, Fix
// Round 1): il bridge USB-SATA espone il block device come ID_BUS=ata, senza
// ID_VENDOR_ID/ID_MODEL_ID (esistono solo sul nodo USB padre). Proprietà
// stabili sul block device: ID_MODEL ("Samsung_Portable_SSD_T1_250GB",
// prefisso comune alle taglie T1, sempre presente) e, solo nello stato
// bloccato, ID_FS_LABEL="T1_SETUP".
static constexpr const char* kT1FsLabel = "T1_SETUP";
static constexpr const char* kT1ModelPrefix = "Samsung_Portable_SSD_T1";

DriveState classify(bool usb_present, bool permission_error,
                    const std::vector<LunInfo>& luns, DriveState previous) {
    if (permission_error) return DriveState::PermissionError;
    if (!usb_present) return DriveState::NoDevice;
    for (const auto& l : luns)
        if (l.has_children) return DriveState::Unlocked;
    for (const auto& l : luns)
        if (!l.has_children &&
            (l.fs_label == "T1_SETUP" || l.size_bytes <= kMaxVirtDisk))
            return DriveState::Locked;
    return previous;  // firma non riconoscibile: Transient (§6)
}

// Dischi T1 via GUdev: enumerator subsystem=block con match DEVTYPE=disk,
// NIENTE match su ID_VENDOR_ID/ID_MODEL_ID (assenti sul block device reale,
// ID_BUS=ata). Filtro in C++: ID_FS_LABEL == "T1_SETUP" oppure ID_MODEL che
// inizia con il prefisso comune "Samsung_Portable_SSD_T1" (il prefisso model
// copre anche lo stato sbloccato, quando ID_FS_LABEL non c'è). I device
// restituiti hanno un riferimento incrementato: liberarli con g_object_unref.
// Verificabile solo su hardware reale.
static std::vector<GUdevDevice*> udev_find_t1_disks() {
    std::vector<GUdevDevice*> out;
    GUdevClient* client = g_udev_client_new(nullptr);
    GUdevEnumerator* en = g_udev_enumerator_new(client);
    g_udev_enumerator_add_match_subsystem(en, "block");
    g_udev_enumerator_add_match_property(en, "DEVTYPE", "disk");
    GList* devices = g_udev_enumerator_execute(en);
    const size_t prefix_len = std::strlen(kT1ModelPrefix);
    for (GList* l = devices; l; l = l->next) {
        GUdevDevice* d = G_UDEV_DEVICE(l->data);
        const gchar* label = g_udev_device_get_property(d, "ID_FS_LABEL");
        const gchar* model = g_udev_device_get_property(d, "ID_MODEL");
        const bool label_t1 = label && std::strcmp(label, kT1FsLabel) == 0;
        const bool model_t1 =
            model && std::strncmp(model, kT1ModelPrefix, prefix_len) == 0;
        if (label_t1 || model_t1)
            out.push_back(static_cast<GUdevDevice*>(g_object_ref(d)));
    }
    g_list_free_full(devices, g_object_unref);
    g_object_unref(en);
    g_object_unref(client);
    return out;
}

// DEVNAME della prima partizione figlia del disco: enumerazione
// subsystem=block con match DEVTYPE=partition (senza match vendor/model:
// sulle partizioni le proprietà vendor/model non sono affidabili), confronto
// per prefisso DEVNAME contro il DEVNAME del disco; il controllo sulla cifra
// dopo il prefisso evita falsi positivi tipo "/dev/sda" vs "/dev/sdaa1"
// (partizione del disco "/dev/sdaa"). Ritorna il DEVNAME con prefisso /dev/
// (il prepend difensivo copre udev DB con DEVNAME relativo su udev meno
// recenti), "" se assente. Verificabile solo su hardware reale.
static std::string udev_first_partition_devname(GUdevDevice* disk) {
    const gchar* disk_name = g_udev_device_get_device_file(disk);  // DEVNAME
    if (!disk_name) return "";
    const size_t disk_len = std::strlen(disk_name);

    GUdevClient* client = g_udev_client_new(nullptr);
    GUdevEnumerator* en = g_udev_enumerator_new(client);
    g_udev_enumerator_add_match_subsystem(en, "block");
    g_udev_enumerator_add_match_property(en, "DEVTYPE", "partition");
    GList* devices = g_udev_enumerator_execute(en);
    std::string found;
    for (GList* l = devices; l && found.empty(); l = l->next) {
        GUdevDevice* d = G_UDEV_DEVICE(l->data);
        const gchar* devname = g_udev_device_get_device_file(d);  // DEVNAME
        if (!devname) continue;
        if (std::strncmp(devname, disk_name, disk_len) == 0 &&
            std::isdigit(static_cast<unsigned char>(devname[disk_len])))
            found = devname;
    }
    g_list_free_full(devices, g_object_unref);
    g_object_unref(en);
    g_object_unref(client);
    if (!found.empty() && found.rfind("/dev/", 0) != 0)
        found = "/dev/" + found;  // udev DB: DEVNAME relativo su udev meno recenti
    return found;
}

static bool udev_has_partitions(GUdevDevice* disk) {
    return !udev_first_partition_devname(disk).empty();
}

std::vector<LunInfo> udev_list_t1_luns() {
    std::vector<LunInfo> out;
    for (GUdevDevice* disk : udev_find_t1_disks()) {
        LunInfo li;
        const gchar* serial = g_udev_device_get_property(disk, "ID_SERIAL_SHORT");
        if (serial) li.serial = serial;
        li.size_bytes = g_udev_device_get_property_as_uint64(disk, "SIZE");
        const gchar* label = g_udev_device_get_property(disk, "ID_FS_LABEL");
        if (label) li.fs_label = label;
        li.has_children = udev_has_partitions(disk);
        out.push_back(li);
        g_object_unref(disk);
    }
    return out;
}

std::string udev_t1_partition_devname() {
    std::string found;
    for (GUdevDevice* disk : udev_find_t1_disks()) {
        found = udev_first_partition_devname(disk);
        g_object_unref(disk);
        if (!found.empty()) return found;
    }
    return found;
}

std::vector<LunInfo> UdevBlockDevInfo::list_t1_luns() {
    return udev_list_t1_luns();
}
