#include "mainwindow.hpp"
#include "mount_udisks.hpp"
#include <libusb.h>  // LIBUSB_ERROR_ACCESS in poll() (protocol.hpp non include libusb, come usb_io.hpp)
#include <cstdio>
#include <chrono>
#include <string.h>  // explicit_bzero della copia della password nel worker (§8)

MainWindow::MainWindow(bool debug_bot) : debug_bot_(debug_bot) {
    set_title("T1 Unlocker");

    // Icona finestra: dentro AppImage i PNG stanno in $APPDIR/usr/share/icons;
    // il fallback a icon-name copre le installazioni da pacchetto. Senza di
    // questa la finestra mostra il placeholder "missing image" (X).
    {
        const char* appdir = g_getenv("APPDIR");
        // Dentro AppImage i PNG sono in $APPDIR/usr/share/...; su sistema in
        // /usr/share/... (review: senza il segmento "usr" il pixbuf non era
        // mai trovato e restava solo il fallback icon-name).
        const std::string base = appdir ? std::string(appdir) + "/usr" : std::string("/usr");
        bool ok = false;
        for (const char* rel : {"share/icons/hicolor/256x256/apps/t1-unlocker.png",
                                "share/icons/hicolor/128x128/apps/t1-unlocker.png",
                                "share/icons/hicolor/48x48/apps/t1-unlocker.png"}) {
            try {
                auto pb = Gdk::Pixbuf::create_from_file(base + "/" + rel);
                set_icon(pb);
                ok = true;
                break;
            } catch (...) { /* prova la prossima dimensione */ }
        }
        if (!ok) set_default_icon_name("t1-unlocker");
    }

    set_default_size(440, 280);
    set_border_width(12);

    if (debug_bot_) {
        // Il trace parte solo su azione (worker): accoda le righe esadecimali,
        // senza toccare i widget GTK dal worker thread (vedi flush_trace).
        proto_.set_trace([this](const char* tag, const uint8_t* p, size_t n) {
            char b[4]; std::string hex;
            for (size_t i = 0; i < n; ++i) { snprintf(b, sizeof b, "%02x ", p[i]); hex += b; }
            std::lock_guard<std::mutex> lk(trace_mtx_);
            trace_pending_.emplace_back(Glib::ustring("[BOT ") + tag + "] " + hex);
        });
    }

    status_.set_text("Status: checking…");
    pw_.set_visibility(false);
    pw_.set_activates_default(true);            // Invio = Sblocca (§6)
    pw_.get_accessible()->set_name("Drive password");
    show_pw_.set_label("Show password");
    unlock_btn_.set_label("Unlock");
    unlock_btn_.set_sensitive(false);
    unlock_btn_.set_can_default(true);
    set_default(unlock_btn_);                   // rende effettivo Invio = Sblocca
    unlock_btn_.get_accessible()->set_name("Unlock");
    log_.set_editable(false);
    log_.set_cursor_visible(false);
    log_buf_ = log_.get_buffer();
    log_line("T1 Unlocker started" + std::string(debug_bot_ ? " (BOT debug)" : ""));

    pw_.signal_changed().connect(sigc::mem_fun(*this, &MainWindow::refresh_ui));
    unlock_btn_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_unlock_clicked));
    done_.connect(sigc::mem_fun(*this, &MainWindow::on_worker_done));
    Glib::signal_timeout().connect(sigc::mem_fun(*this, &MainWindow::poll), 2000);

    auto row_pw = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
    row_pw->pack_start(pw_, Gtk::PACK_EXPAND_WIDGET);
    row_pw->pack_start(show_pw_, Gtk::PACK_SHRINK);
    auto scroll = Gtk::manage(new Gtk::ScrolledWindow);
    scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_ALWAYS);
    scroll->add(log_);
    auto root = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 8));
    root->pack_start(status_, Gtk::PACK_SHRINK);
    root->pack_start(*row_pw, Gtk::PACK_SHRINK);
    root->pack_start(unlock_btn_, Gtk::PACK_SHRINK);
    root->pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);
    add(*root);
    show_all_children();

    show_pw_.signal_toggled().connect([this]() {
        pw_.set_visibility(show_pw_.get_active());
    });
}

// Il polling si sospende durante le operazioni (busy_, §6) e il rilevamento non
// fa claim: usb_.close() subito dopo l'apertura e' intenzionale (release/close
// senza claim sono innocui; il claim avviene solo nel worker).
bool MainWindow::poll() {
    if (busy_) return true;  // polling sospeso durante operazioni (§6)
    int rc = usb_.open_first({0x8001, 0x61f2, 0x61f1});
    bool usb_present = (rc == 0);
    bool perm = (rc == LIBUSB_ERROR_ACCESS);
    if (usb_present) usb_.close();
    auto luns = udev_list_t1_luns();
    state_ = classify(usb_present, perm, luns, state_);
    refresh_ui();
    return true;  // mantiene attivo il timeout (signal_timeout richiede bool)
}

void MainWindow::on_unlock_clicked() {
    if (busy_ || state_ != DriveState::Locked) return;
    start_worker(pw_.get_text());
}

void MainWindow::start_worker(std::string pw) {
    pw_.set_text("");  // §8: la copia GUI sparisce subito, prima di lanciare il worker
    busy_ = true; refresh_ui();
    log_line("Unlocking…");
    // Cattura per move: un'unica copia della password, azzerata nel worker (§8).
    // Serve init-capture move + mutable: la copia di un const& resterebbe const.
    worker_ = std::thread([this, pw = std::move(pw)]() mutable {
        worker_result_ = UnlockResult::TransferError;
        mount_msg_.clear();
        if (usb_.open_first({0x8001, 0x61f2, 0x61f1}) == 0) {
            if (usb_.claim_interface(0) == 0) {
                worker_result_ = proto_.unlock(pw);
                // §8: wipe best-effort della copia locale su OGNI esito di unlock
                // (riuscito o fallito), prima di relink/mount/emit.
                if (!pw.empty()) explicit_bzero(&pw[0], pw.size());
                proto_.relink();  // §7: relink sempre (ri-presenta bloccato se fallito)
                // Misura hardware 2026-11-16: il relink da solo non ri-presenta
                // il LUN reale (usb-storage resta sulla vista stantia 128M);
                // il port reset forza la ri-enumerazione. Best-effort: rc
                // ignorato, l'esito reale lo verifica l'attesa partizione.
                // Reset incondizionato: utile anche nel caso "già sbloccato".
                usb_.reset();
                // Misura 2026-11-16 (run 09:01): la partizione appare solo DOPO
                // il release dell'interfaccia (re-attach di usb-storage → probe
                // del LUN reale, ~14 s). Con il close in fondo al worker il
                // probe partiva a fine attesa: sempre fuori finestra. Chiudendo
                // subito, il probe parte qui e l'attesa sottostante lo copre.
                usb_.close();  // doppio close sicuro (close è no-op con h_ nullo)
                if (worker_result_ == UnlockResult::Ok) {
                    // Risoluzione controller #1: l'attesa della partizione e il
                    // mount restano NEL worker (udisks2_mount blocca fino a 5 s,
                    // mai nel main loop). Poll 60×500 ms (~30 s): su hardware
                    // reale la ri-presentazione post-relink può tardare ben oltre
                    // i 6 s iniziali (misurato: partizione apparsa solo dopo un
                    // secondo ciclo di relink, minuti dopo).
                    std::string dev;
                    for (int i = 0; i < 60 && dev.empty(); ++i) {
                        dev = udev_t1_partition_devname();
                        if (dev.empty())
                            std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                    if (dev.empty())
                        mount_msg_ = "Partition not visible within 30 s: it will be picked up by the status polling but must be mounted manually (udisksctl mount -b /dev/sdX1)";
                    else if (udisks2_mount(dev))
                        mount_msg_ = "Partition " + dev + " mounted";
                    else
                        mount_msg_ = "Automatic mount of " + dev + " failed: mount it manually";
                }
            }
        }
        // §8: wipe INCONDIZIONATO della copia locale: copre anche i path in cui
        // open/claim falliscono e unlock non viene mai eseguito (review finale:
        // la distruzione della stringa senza bzero su quei path è violazione §8).
        if (!pw.empty()) explicit_bzero(&pw[0], pw.size());
        usb_.close();
        done_.emit();  // on_worker_done legge i risultati DOPO il join (visibilità garantita)
    });
}

void MainWindow::on_worker_done() {
    if (worker_.joinable()) worker_.join();  // sincronizza con tutti gli accessi del worker
    flush_trace();  // righe [BOT ...] generate nel worker, ora in sicurezza sul main thread
    busy_ = false;
    if (worker_result_ == UnlockResult::Ok) {
        log_line("Unlock successful");
        if (!mount_msg_.empty()) log_line(mount_msg_);  // esito del mount fatto nel worker
    } else if (worker_result_ == UnlockResult::WrongPassword) {
        // resp[9]=0x02 si ottiene anche tentando unlock su un disco già
        // sbloccato (misurato su hardware): il messaggio lo rende esplicito.
        log_line("Wrong password (or drive already unlocked) — drive re-presented locked; after repeated failures unplug and replug the drive");
        Gtk::MessageDialog d(*this, "Wrong password (or drive already unlocked)", false, Gtk::MESSAGE_ERROR);
        d.run();
    } else {
        log_line("USB communication error during unlock");
        Gtk::MessageDialog d(*this, "USB error", false, Gtk::MESSAGE_ERROR);
        d.run();
    }
    refresh_ui();  // pw_.set_text("") e' stato anticipato in start_worker (§8)
}

void MainWindow::refresh_ui() {
    Glib::ustring s = "Status: ";
    switch (state_) {
        case DriveState::NoDevice:       s += "No drive detected"; break;
        case DriveState::Locked:         s += "Locked drive detected"; break;
        case DriveState::Unlocked:       s += "Drive unlocked"; break;
        case DriveState::Transient:      s += "Drive in transition…"; break;
        case DriveState::PermissionError:
            s += "Insufficient permissions (udev/uaccess) — see README";
            break;
    }
    status_.set_text(s);
    unlock_btn_.set_sensitive(state_ == DriveState::Locked && !busy_ && !pw_.get_text().empty());
}

void MainWindow::log_line(const Glib::ustring& s) {
    log_buf_->insert_at_cursor(s + "\n");
    auto end = log_buf_->end();
    log_.scroll_to(end);
}

void MainWindow::flush_trace() {
    std::lock_guard<std::mutex> lk(trace_mtx_);
    for (const auto& s : trace_pending_) log_line(s);
    trace_pending_.clear();
}

bool MainWindow::on_delete_event(GdkEventAny*) {
    if (busy_) {
        Gtk::MessageDialog d(*this, "Operation in progress: wait for it to finish before closing",
                             false, Gtk::MESSAGE_INFO);
        d.run();
        return true;  // blocca la chiusura (§6)
    }
    return false;
}

MainWindow::~MainWindow() {
    if (worker_.joinable()) worker_.join();
}
