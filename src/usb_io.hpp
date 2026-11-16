#pragma once
#include <cstdint>
#include <vector>

class UsbIo {
public:
    virtual ~UsbIo() = default;
    virtual int open_first(const std::vector<uint16_t>& pids) = 0;  // 0 ok; LIBUSB_ERROR_ACCESS ⇒ PermissionError
    virtual int claim_interface(int n) = 0;
    virtual int reset() = 0;  // port reset: forza ri-enumerazione (post-relink); 0 ok
    virtual int bulk_write(const uint8_t* d, size_t len, int timeout_ms) = 0;
    virtual int bulk_read(uint8_t* d, size_t len, int timeout_ms) = 0;
    virtual void close() = 0;
};

class LibusbIo : public UsbIo {
public:
    LibusbIo();   // libusb_init
    ~LibusbIo() override;  // close + libusb_exit
    int open_first(const std::vector<uint16_t>& pids) override;
    int claim_interface(int n) override;
    int reset() override;
    int bulk_write(const uint8_t* d, size_t len, int timeout_ms) override;
    int bulk_read(uint8_t* d, size_t len, int timeout_ms) override;
    void close() override;
private:
    struct libusb_device_handle* h_ = nullptr;  // pimpl per non includere libusb nell'header
};
