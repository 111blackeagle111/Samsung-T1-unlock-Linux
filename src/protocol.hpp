#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "usb_io.hpp"

enum class UnlockResult { Ok, WrongPassword, TransferError };

class Protocol {
public:
    explicit Protocol(UsbIo& io) : io_(io) {}
    // Hook di trace CBW/CSW per il debug BOT (Task 6): invocato SOLO sui
    // pacchetti di comando (CBW, 31 byte) e di stato (CSW, 13 byte) — MAI sul
    // payload password, per non esporre mai il segreto nel log.
    using TraceFn = std::function<void(const char* tag, const uint8_t* p, size_t n)>;
    void set_trace(TraceFn fn) { trace_ = std::move(fn); }
    std::vector<uint8_t> unlock_cbw() const;
    std::vector<uint8_t> relink_cbw() const;
    std::vector<uint8_t> password_payload(const std::string& pw) const;
    UnlockResult unlock(const std::string& password);
    int relink();
private:
    void trace(const char* tag, const uint8_t* p, size_t n) {
        if (trace_) trace_(tag, p, n);
    }
    UsbIo& io_;
    TraceFn trace_;
};
