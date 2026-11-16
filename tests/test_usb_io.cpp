#include <gtest/gtest.h>
#include "../src/usb_io.hpp"

TEST(UsbIoTest, InitRaiiNoDevice) {
    LibusbIo io;  // ctor/dtor: libusb_init/exit senza crash
    std::vector<uint16_t> pids{0xffff};  // PID inesistente
    EXPECT_LT(io.open_first(pids), 0);   // errore atteso, non 0
}

TEST(UsbIoTest, BulkSenzaHandle) {
    LibusbIo io;
    uint8_t buf[8] = {0};
    EXPECT_LT(io.bulk_write(buf, sizeof(buf), 100), 0);
    EXPECT_LT(io.bulk_read(buf, sizeof(buf), 100), 0);
}

TEST(UsbIoTest, ResetSenzaHandle) {
    LibusbIo io;
    EXPECT_LT(io.reset(), 0);  // handle nullo: errore atteso, non crash
}
