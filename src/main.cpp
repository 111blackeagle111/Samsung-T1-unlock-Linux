#include <gtkmm.h>
#include <cstring>
#include "mainwindow.hpp"

// Risoluzione controller #2: il flag --debug-bot viene parseato qui in main()
// e passato a MainWindow(debug_bot). La Gtk::Application viene creata SENZA
// argc/argv: il parser opzioni di gtk_init esce con errore sulle opzioni
// custom (come --debug-bot), mentre GtkApplication inizializza comunque GTK
// allo startup. Il polling di stato resta identico senza il flag.
int main(int argc, char* argv[]) {
    bool debug_bot = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--debug-bot") == 0) debug_bot = true;

    auto app = Gtk::Application::create("io.github.111blackeagle111.t1unlocker");
    MainWindow window(debug_bot);
    return app->run(window);
}
