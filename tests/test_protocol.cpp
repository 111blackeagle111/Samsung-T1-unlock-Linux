#include <gtest/gtest.h>
#include <libusb.h>  // LIBUSB_ERROR_IO nel mock (protocol.hpp non include libusb, come usb_io.hpp)
#include <deque>
#include <algorithm>
#include <string.h>
#include "../src/protocol.hpp"

namespace {
struct MockIo : public UsbIo {
    std::vector<std::vector<uint8_t>> writes;
    std::deque<std::vector<uint8_t>> reads;
    int write_rc = 0;
    int open_first(const std::vector<uint16_t>&) override { return 0; }
    int claim_interface(int) override { return 0; }
    int reset() override { return 0; }
    int bulk_write(const uint8_t* d, size_t len, int) override {
        writes.emplace_back(d, d + len);
        return write_rc;
    }
    int bulk_read(uint8_t* d, size_t len, int) override {
        if (reads.empty()) return LIBUSB_ERROR_IO;
        size_t n = std::min(len, reads.front().size());
        memcpy(d, reads.front().data(), n);
        reads.pop_front();
        return 0;
    }
    void close() override {}
};
}  // namespace

namespace {
const uint8_t kUnlockCbw[31] = {0x55,0x53,0x42,0x43, 0x0a,0x00,0x00,0x00,
    0x00,0x02,0x00,0x00, 0x00,0x00,0x10,
    0x85,0x0a,0x26,0x00,0xd6,0x00,0x01,0x00,
    0xc6,0x00,0x4f,0x00,0xc2,0x00,0xb0,0x00};
const uint8_t kRelinkCbw[31] = {0x55,0x53,0x42,0x43, 0x0b,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x06,
    0xe8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
}  // namespace

TEST(ProtocolCbw, UnlockCbwByteEsatti) {
    MockIo io; Protocol p(io);
    auto cbw = p.unlock_cbw();
    ASSERT_EQ(cbw.size(), 31u);
    EXPECT_EQ(0, memcmp(cbw.data(), kUnlockCbw, 31));
}

TEST(ProtocolCbw, RelinkCbwByteEsatti) {
    MockIo io; Protocol p(io);
    auto cbw = p.relink_cbw();
    ASSERT_EQ(cbw.size(), 31u);
    EXPECT_EQ(0, memcmp(cbw.data(), kRelinkCbw, 31));
}

TEST(ProtocolPayload, PasswordZeroPaddedA512) {
    MockIo io; Protocol p(io);
    auto pay = p.password_payload("abc");
    ASSERT_EQ(pay.size(), 512u);
    EXPECT_EQ(0, memcmp(pay.data(), "abc", 3));
    for (size_t i = 3; i < 512; ++i) ASSERT_EQ(0, pay[i]) << "offset " << i;
}

TEST(ProtocolUnlock, SuccessoConResp9Zero) {
    MockIo io; Protocol p(io);
    std::vector<uint8_t> ok(512, 0); ok[9] = 0x00;
    io.reads.push_back(ok);
    io.reads.push_back(std::vector<uint8_t>(13, 0));  // CSW con bCSWStatus=0
    EXPECT_EQ(UnlockResult::Ok, p.unlock("pw"));
    ASSERT_EQ(io.writes.size(), 2u);
    EXPECT_EQ(io.writes[0].size(), 31u);
    EXPECT_EQ(io.writes[1].size(), 512u);
}

TEST(ProtocolUnlock, PasswordErrataConResp9Due) {
    MockIo io; Protocol p(io);
    std::vector<uint8_t> bad(512, 0); bad[9] = 0x02;
    io.reads.push_back(bad);
    io.reads.push_back(std::vector<uint8_t>(13, 0));
    EXPECT_EQ(UnlockResult::WrongPassword, p.unlock("pw"));
}

TEST(ProtocolUnlock, AltroValoreErroreGenerico) {
    MockIo io; Protocol p(io);
    std::vector<uint8_t> other(512, 0); other[9] = 0x7f;
    io.reads.push_back(other);
    io.reads.push_back(std::vector<uint8_t>(13, 0));
    EXPECT_EQ(UnlockResult::TransferError, p.unlock("pw"));
}

TEST(ProtocolUnlock, WriteFallitoTransferError) {
    MockIo io; Protocol p(io);
    io.write_rc = -1;  // es. LIBUSB_ERROR_IO
    EXPECT_EQ(UnlockResult::TransferError, p.unlock("pw"));
    ASSERT_EQ(io.writes.size(), 1u);  // fallita alla CBW: payload mai inviato
}

TEST(ProtocolUnlock, ReadFallitoTransferError) {
    MockIo io; Protocol p(io);
    // write_rc = 0; reads vuote: la prima bulk_read fallisce (LIBUSB_ERROR_IO dal mock)
    EXPECT_EQ(UnlockResult::TransferError, p.unlock("pw"));
    ASSERT_EQ(io.writes.size(), 2u);  // CBW e payload inviati, poi lettura fallita
}
