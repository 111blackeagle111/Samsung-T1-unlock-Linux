#include "protocol.hpp"
#include <libusb.h>  // LIBUSB_ERROR_IO in relink() (protocol.hpp non include libusb, come usb_io.hpp)
#include <string.h>

static const uint8_t kUnlockCbw[31] = {
    0x55,0x53,0x42,0x43, 0x0a,0x00,0x00,0x00,
    0x00,0x02,0x00,0x00, 0x00,0x00,0x10,
    0x85,0x0a,0x26,0x00,0xd6,0x00,0x01,0x00,
    0xc6,0x00,0x4f,0x00,0xc2,0x00,0xb0,0x00};

static const uint8_t kRelinkCbw[31] = {
    0x55,0x53,0x42,0x43, 0x0b,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x06,
    0xe8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

std::vector<uint8_t> Protocol::unlock_cbw() const {
    return std::vector<uint8_t>(kUnlockCbw, kUnlockCbw + 31);
}

std::vector<uint8_t> Protocol::relink_cbw() const {
    return std::vector<uint8_t>(kRelinkCbw, kRelinkCbw + 31);
}

std::vector<uint8_t> Protocol::password_payload(const std::string& pw) const {
    std::vector<uint8_t> pay(512, 0);
    size_t n = pw.size() < 511 ? pw.size() : 511;  // limite fisico payload (§5)
    memcpy(pay.data(), pw.data(), n);
    return pay;
}

UnlockResult Protocol::unlock(const std::string& password) {
    auto cbw = unlock_cbw();
    trace("unlock-cbw", cbw.data(), cbw.size());  // solo CBW 31 byte, mai il payload
    if (io_.bulk_write(cbw.data(), cbw.size(), 3000) != 0)
        return UnlockResult::TransferError;
    auto pay = password_payload(password);
    if (io_.bulk_write(pay.data(), pay.size(), 3000) != 0) {
        explicit_bzero(pay.data(), pay.size());  // §8: azzera il payload anche sui percorsi d'errore
        return UnlockResult::TransferError;
    }

    std::vector<uint8_t> resp(512, 0);
    if (io_.bulk_read(resp.data(), resp.size(), 3000) != 0) {
        explicit_bzero(pay.data(), pay.size());  // §8: come sopra
        return UnlockResult::TransferError;
    }

    // CSW leniente (§5): se arriva deve avere bCSWStatus==0; timeout ⇒ solo warning
    uint8_t csw[13] = {0};
    io_.bulk_read(csw, sizeof(csw), 3000);  // esito ignorato di proposito
    trace("csw", csw, sizeof(csw));  // solo CSW 13 byte

    explicit_bzero(pay.data(), pay.size());  // password azzerata (§8)

    if (resp[9] == 0x02) return UnlockResult::WrongPassword;
    if (resp[9] != 0x00) return UnlockResult::TransferError;
    return UnlockResult::Ok;
}

int Protocol::relink() {
    auto cbw = relink_cbw();
    trace("relink-cbw", cbw.data(), cbw.size());  // solo CBW 31 byte
    if (io_.bulk_write(cbw.data(), cbw.size(), 3000) != 0)
        return LIBUSB_ERROR_IO;
    uint8_t resp[512] = {0};
    io_.bulk_read(resp, sizeof(resp), 3000);  // esito ignorato (come pssdunlock)
    return 0;
}
