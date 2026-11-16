#include <gtest/gtest.h>
#include "../src/block_dev_info.hpp"

namespace {
LunInfo locked_lun() {
    return {"T1FIXTURE000001", 128ull * 1024ull * 1024ull, false, "T1_SETUP"};
}
LunInfo unlocked_lun() {
    return {"T1FIXTURE000001", 250ull * 1000ull * 1000ull * 1000ull, true, ""};
}
LunInfo piccolo_senza_label() {  // variante T1 classico: piccolo, senza label
    return {"X", 512ull * 1024ull * 1024ull, false, ""};
}
}  // namespace

TEST(Detect, NessunUsbNoDevice) {
    EXPECT_EQ(DriveState::NoDevice,
              classify(false, false, {}, DriveState::Unlocked));
}

TEST(Detect, PermessoNegato) {
    EXPECT_EQ(DriveState::PermissionError,
              classify(true, true, {locked_lun()}, DriveState::Locked));
}

TEST(Detect, DiscoConPartizioniUnlocked) {
    EXPECT_EQ(DriveState::Unlocked,
              classify(true, false, {unlocked_lun()}, DriveState::Locked));
}

TEST(Detect, DiscoVirtualeLabelLocked) {
    EXPECT_EQ(DriveState::Locked,
              classify(true, false, {locked_lun()}, DriveState::NoDevice));
}

TEST(Detect, DiscoVirtualePiccoloSenzaLabelLocked) {
    EXPECT_EQ(DriveState::Locked,
              classify(true, false, {piccolo_senza_label()}, DriveState::NoDevice));
}

TEST(Detect, FirmaAssenteMantieneStato) {
    LunInfo ambiguo{"S", 100ull * 1024ull * 1024ull * 1024ull, false, ""};
    EXPECT_EQ(DriveState::Transient,
              classify(true, false, {ambiguo}, DriveState::Transient));
}
