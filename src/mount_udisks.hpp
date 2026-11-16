#pragma once
#include <string>

// Monta il block device via org.freedesktop.UDisks2.Filesystem.Mount (system bus).
// ATTENZIONE: chiamata BLOCCANTE fino a 5 s (timeout D-Bus): invocare SOLO dal
// worker thread, mai dal main loop GTK (Task 6, risoluzione controller).
bool udisks2_mount(const std::string& block_dev);  // es. "/dev/sda1"
