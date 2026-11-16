#include "mount_udisks.hpp"

#include <giomm.h>

#include <exception>
#include <iostream>
#include <map>
#include <vector>

// Deviazioni dal brief (giomm 2.64 / gtkmm-3.24 su ubuntu:20.04):
// - La firma non-cancellable di Connection::call_sync e'
//   call_sync(object_path, interface_name, method_name, parameters,
//             bus_name = "", timeout_msec = -1, flags, reply_type):
//   bus_name va passato in 5a posizione (il brief lo metteva al 2o).
// - parameters deve essere un VariantContainerBase "(a{sv})": la tupla via
//   std::tuple<Options> serializza "(v)" (dizionario incapsulato in variant
//   generico) e udisks2 la rifiuta con InvalidArgs (misura 2026-11-16);
//   create_tuple compone i figli direttamente → "(a{sv})", la firma attesa.
// - Gio::init() e' idempotente e rende il modulo autoportante; la GUI del
//   Task 6 lo fara' comunque via Gtk::Application senza conflitti.
// - Catture aggiuntive (std::exception, ...) per garantire best-effort:
//   nessuna eccezione raggiunge mai il chiamante.
bool udisks2_mount(const std::string& block_dev) {
    if (block_dev.rfind("/dev/", 0) != 0) return false;
    try {
        Gio::init();
        auto conn = Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SYSTEM);
        const std::string name = block_dev.substr(block_dev.find_last_of('/') + 1);
        // I nomi D-Bus sono case-sensitive: UDisks2 va in maiuscolo (misura
        // 2026-11-16: con i nomi minuscoli la chiamata riceve UnknownMethod
        // "Object does not exist at path" e il mount non avviene mai).
        const std::string path = "/org/freedesktop/UDisks2/block_devices/" + name;
        using Options = Glib::Variant<std::map<Glib::ustring, Glib::VariantBase>>;
        // Verificato nel container: get_type_string() → "(a{sv})".
        const auto params = Glib::VariantContainerBase::create_tuple(
            std::vector<Glib::VariantBase>{Options::create({})});
        conn->call_sync(path, "org.freedesktop.UDisks2.Filesystem", "Mount", params,
                        "org.freedesktop.UDisks2", 5000);
        std::cout << "Mounted " << block_dev << " via udisks2\n";
        return true;
    } catch (const Glib::Error& e) {
        // Il DE può vincere la gara di auto-mount appena la partizione appare:
        // "già montato" è un successo, non un errore (misura 2026-11-16).
        const Glib::ustring remote = Gio::DBus::ErrorUtils::get_remote_error(e);
        const Glib::ustring msg(e.what());
        const bool gia_montata =
            remote.find("org.freedesktop.UDisks2.Error.AlreadyMounted") != Glib::ustring::npos ||
            msg.find("AlreadyMounted") != Glib::ustring::npos;
        if (gia_montata) {
            std::cout << block_dev << " already mounted (udisks2)\n";
            return true;
        }
        std::cerr << "udisks2 mount unavailable for " << block_dev
                  << ": " << e.what() << "\n";
        return false;
    } catch (const std::exception& e) {
        std::cerr << "udisks2 mount unavailable for " << block_dev
                  << ": " << e.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "udisks2 mount unavailable for " << block_dev
                  << ": unknown error\n";
        return false;
    }
}
