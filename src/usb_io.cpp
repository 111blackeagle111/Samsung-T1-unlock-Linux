#include "usb_io.hpp"
#include <libusb.h>

LibusbIo::LibusbIo() { libusb_init(nullptr); }
LibusbIo::~LibusbIo() { close(); libusb_exit(nullptr); }

int LibusbIo::open_first(const std::vector<uint16_t>& pids) {
    close();
    libusb_device** list = nullptr;
    ssize_t n = libusb_get_device_list(nullptr, &list);
    if (n < 0) return static_cast<int>(n);
    int rc = LIBUSB_ERROR_NOT_FOUND;  // nessun device corrispondente
    for (ssize_t i = 0; i < n; ++i) {
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (desc.idVendor != 0x04e8) continue;
        bool match = false;
        for (uint16_t pid : pids) {
            if (desc.idProduct == pid) { match = true; break; }
        }
        if (!match) continue;
        rc = libusb_open(list[i], &h_);  // propaga es. LIBUSB_ERROR_ACCESS; 0 se ok
        break;  // primo T1 corrispondente: ritorna subito il suo codice
    }
    libusb_free_device_list(list, 1);  // unref dei riferimenti della lista, sempre
    return rc;
}

int LibusbIo::claim_interface(int n) {
    if (!h_) return LIBUSB_ERROR_NOT_FOUND;
    // Reset prima del claim (parity col CLI di riferimento): il bridge Samsung
    // può partire con stato BOT sporco (usb-storage sul CD virtuale montato);
    // senza reset il primo CBW va in stall e tutti i transfer successivi
    // falliscono con PIPELINE. Nota: su device con kernel driver attaccato il
    // reset può causare re-enumerazione (l'handle può risultare invalidato:
    // i bulk riporteranno NOT_FOUND).
    int rr = libusb_reset_device(h_);
    if (rr != 0) return rr;
    libusb_set_auto_detach_kernel_driver(h_, 1);
    return libusb_claim_interface(h_, n);
}

int LibusbIo::reset() {
    if (!h_) return LIBUSB_ERROR_NOT_FOUND;
    // Port reset post-relink: forza la ri-enumerazione così usb-storage
    // ri-legge il LUN reale (misura 2026-11-16: senza reset la vista resta
    // stantia a 128M e la partizione non appare).
    return libusb_reset_device(h_);
}

int LibusbIo::bulk_write(const uint8_t* d, size_t len, int timeout_ms) {
    if (!h_) return LIBUSB_ERROR_NOT_FOUND;
    int t = 0;
    int r = libusb_bulk_transfer(h_, 0x02, const_cast<uint8_t*>(d), int(len), &t, timeout_ms);
    if (r == LIBUSB_ERROR_PIPE) {  // stall (PIPE è -9, non -5)
        // Stall recovery: clear_halt sull'endpoint OUT e un solo retry, parity
        // col CLI (un CBW stallato va sbloccato prima di ritentare).
        libusb_clear_halt(h_, 0x02);
        t = 0;
        r = libusb_bulk_transfer(h_, 0x02, const_cast<uint8_t*>(d), int(len), &t, timeout_ms);
    }
    return r;
}

int LibusbIo::bulk_read(uint8_t* d, size_t len, int timeout_ms) {
    if (!h_) return LIBUSB_ERROR_NOT_FOUND;
    int t = 0;
    int r = libusb_bulk_transfer(h_, 0x81, d, int(len), &t, timeout_ms);
    if (r == LIBUSB_ERROR_PIPE) {  // stall (PIPE è -9, non -5)
        // Stall recovery: clear_halt sull'endpoint IN e un solo retry, parity
        // col CLI (un CSW stallato va sbloccato prima di ritentare).
        libusb_clear_halt(h_, 0x81);
        t = 0;
        r = libusb_bulk_transfer(h_, 0x81, d, int(len), &t, timeout_ms);
    }
    return r;
}

void LibusbIo::close() {
    if (h_) { libusb_release_interface(h_, 0); libusb_close(h_); h_ = nullptr; }
}
