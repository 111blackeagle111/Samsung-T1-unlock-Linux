#pragma once
#include <gtkmm.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "usb_io.hpp"
#include "protocol.hpp"
#include "block_dev_info.hpp"

class MainWindow : public Gtk::Window {
public:
    MainWindow(bool debug_bot);
    ~MainWindow() override;
protected:
    bool on_delete_event(GdkEventAny* e) override;
private:
    bool poll();  // true: mantiene attivo il timeout (signal_timeout richiede bool)
    void on_unlock_clicked();
    void start_worker(std::string pw);  // per valore: la copia catturata dal worker viene azzerata (§8)
    void on_worker_done();
    void log_line(const Glib::ustring& s);
    void flush_trace();  // svuota sul main thread le righe [BOT ...] del worker
    void refresh_ui();

    LibusbIo usb_;
    Protocol proto_{usb_};
    DriveState state_ = DriveState::NoDevice;
    bool busy_ = false;
    bool debug_bot_ = false;

    std::thread worker_;
    UnlockResult worker_result_ = UnlockResult::TransferError;
    Glib::Dispatcher done_;
    std::string mount_msg_;  // scritto dal worker, letto in on_worker_done DOPO il join

    // Il trace BOT arriva dal worker thread: i widget GTK non sono thread-safe,
    // quindi le righe vengono accodate qui e svuotate dal main thread.
    std::mutex trace_mtx_;
    std::vector<Glib::ustring> trace_pending_;

    Gtk::Label status_;
    Gtk::Entry pw_;
    Gtk::CheckButton show_pw_;
    Gtk::Button unlock_btn_;
    Gtk::TextView log_;
    Glib::RefPtr<Gtk::TextBuffer> log_buf_;
};
