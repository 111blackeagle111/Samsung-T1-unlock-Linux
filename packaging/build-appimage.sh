#!/usr/bin/env bash
# Build completa del progetto + AppImage (T1Unlocker-<versione>-x86_64.AppImage)
# via linuxdeploy + linuxdeploy-plugin-gtk, con bundling GTK3.
#
# Note procedurali (dallo spike del Task 1, vedi docs/spike-packaging.md):
# - linuxdeploy-plugin-gtk non è più distribuito come AppImage ma come script
#   bash: scaricato da raw.githubusercontent.com e pinnato a un commit SHA
#   fisso (ultimo che ha modificato lo script) per la riproducibilità.
# - linuxdeploy viene eseguito NEL CONTAINER t1-unlocker-build: lo script del
#   plugin richiede pkg-config, dev librsvg/gobject-introspection e
#   gdk-pixbuf-query-loaders, presenti nell'immagine ma non sull'host.
# - Il runtime type2 non è scaricabile dal container (rete in `docker run`
#   bloccata sul download): scaricato dall'host in build/ e passato a
#   linuxdeploy-plugin-appimage via LDAI_RUNTIME_FILE.
# - libudev NON viene bundle-ata (--exclude-library): deve combaciare col
#   daemon udev dell'host (accoppiamento ABI con systemd); a runtime si usa
#   quella di sistema. libgudev invece va bundle-ata: è accoppiata alla glib
#   del processo, e la glib bundle (più vecchia) non soddisfa i simboli che
#   le libgudev delle distro recenti si aspettano (verificato su host con
#   glib >= 2.80: "undefined symbol: g_once_init_enter_pointer").
#   Attenzione: il plugin-gtk re-invoca internamente linuxdeploy SENZA i
#   --exclude-library e bundle-a anche libudev: il deployment e il packaging
#   sono quindi divisi in due passaggi (5a/5c), rimuovendo libudev
#   dall'AppDir nel passaggio intermedio (5b).
# - Un apprun-hook punta GIO_MODULE_DIR ai moduli dentro l'AppImage: evita che
#   l'app carichi i moduli GIO dell'host (compilati contro glib più recente,
#   es. errore g_task_set_static_name visto nello spike).
# - Il nome del file di output è forzato con LDAI_OUTPUT: resta indipendente
#   da Name= del desktop file (che contiene lo spazio, "T1 Unlocker").
# - Cleanup iniziale limitato agli artefatti prodotti da questo script
#   (git-ignored, rigenerabili): build/, AppDir/, T1Unlocker-*.AppImage.
set -euo pipefail
cd "$(dirname "$0")/.."

# Pin riproducibile del plugin-gtk: ultimo commit che tocca linuxdeploy-plugin-gtk.sh
PLUGIN_GTK_SHA=3b67a1d1c1b0c8268f57f2bce40fe2d33d409cea
VERSION=0.1.0

docker build -t t1-unlocker-build packaging/

rm -rf build AppDir T1Unlocker-*.AppImage

# 1) Build completa del progetto (release, test inclusi) nel container
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src t1-unlocker-build \
    bash -c "cd /src && meson setup build --buildtype=release >/dev/null && ninja -C build \
             && rsvg-convert -w 256 -h 256 packaging/icon.svg -o packaging/icon_256.png \
             && rsvg-convert -w 128 -h 128 packaging/icon.svg -o packaging/icon_128.png \
             && rsvg-convert -w 48 -h 48 packaging/icon.svg -o packaging/icon_48.png"

# 2) AppDir: binario reale + desktop + appdata + icone
#    (libt1core.so è la libreria condivisa interna del progetto, DT_NEEDED
#    del binario: va nell'AppDir, linuxdeploy ne deploya le dipendenze e
#    imposta l'rpath di usr/bin verso usr/lib)
mkdir -p AppDir/usr/bin AppDir/usr/lib AppDir/usr/share/applications AppDir/usr/share/metainfo
mkdir -p AppDir/usr/share/icons/hicolor/256x256/apps AppDir/usr/share/icons/hicolor/128x128/apps AppDir/usr/share/icons/hicolor/48x48/apps AppDir/usr/share/icons/hicolor/scalable/apps
cp build/src/t1-unlocker AppDir/usr/bin/t1-unlocker
cp build/src/libt1core.so AppDir/usr/lib/
cp packaging/io.github.111blackeagle111.t1unlocker.desktop AppDir/usr/share/applications/
cp packaging/t1-unlocker.appdata.xml AppDir/usr/share/metainfo/
cp packaging/icon_256.png AppDir/usr/share/icons/hicolor/256x256/apps/t1-unlocker.png
cp packaging/icon_128.png AppDir/usr/share/icons/hicolor/128x128/apps/t1-unlocker.png
cp packaging/icon_48.png AppDir/usr/share/icons/hicolor/48x48/apps/t1-unlocker.png
cp packaging/icon_256.png AppDir/t1-unlocker.png
cp packaging/icon.svg AppDir/usr/share/icons/hicolor/scalable/apps/t1-unlocker.svg

# 3) Hook AppRun: GIO_MODULE_DIR dentro l'AppImage (la dir esiste ed è vuota:
#    l'app non usa gvfs — il mount passa da D-Bus/udisks2 — quindi non serve
#    alcun modulo GIO, e non vengono caricati quelli dell'host)
mkdir -p AppDir/apprun-hooks AppDir/usr/lib/gio/modules
cat > AppDir/apprun-hooks/t1-unlocker-gio.sh <<'EOF'
# Mitigazione warning moduli GIO host (es. g_task_set_static_name su gvfs):
# la glib bundle è più vecchia di quella dell'host; GIO_MODULE_DIR punta a una
# directory vuota dentro l'AppImage così nessun modulo esterno viene caricato.
export APPDIR="${APPDIR:-"$(dirname "$(realpath "$0")")"}"
export GIO_MODULE_DIR="$APPDIR/usr/lib/gio/modules"
EOF

# 4) Strumenti (download dall'host: la rete nel container è limitata ad apt)
mkdir -p build
wget -q -O linuxdeploy.AppImage https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget -q -O linuxdeploy-plugin-gtk "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/$PLUGIN_GTK_SHA/linuxdeploy-plugin-gtk.sh"
wget -q -O build/runtime-x86_64 https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
chmod +x linuxdeploy.AppImage linuxdeploy-plugin-gtk build/runtime-x86_64

# 5a) linuxdeploy nel container: bundling GTK3 via plugin-gtk (deployment
#     senza output AppImage; vedi 5b). LD_LIBRARY_PATH serve a ldd (usato da
#     linuxdeploy per tracciare i NEEDED) a risolvere libt1core.so dentro
#     l'AppDir: gli rpath definitivi vengono impostati da linuxdeploy a fine
#     deployment.
export APPIMAGE_EXTRACT_AND_RUN=1
export DEPLOY_GTK_VERSION=3
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
export LDAI_OUTPUT="T1Unlocker-${VERSION}-x86_64.AppImage"
export LDAI_RUNTIME_FILE=/src/build/runtime-x86_64
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src \
    -e APPIMAGE_EXTRACT_AND_RUN -e DEPLOY_GTK_VERSION -e LINUXDEPLOY_OUTPUT_VERSION \
    -e LDAI_OUTPUT -e LDAI_RUNTIME_FILE \
    t1-unlocker-build \
    bash -c "export PATH=/src:\$PATH && export LD_LIBRARY_PATH=/src/AppDir/usr/lib && ./linuxdeploy.AppImage --appdir AppDir \
        -e AppDir/usr/bin/t1-unlocker \
        -l AppDir/usr/lib/libt1core.so \
        -d packaging/io.github.111blackeagle111.t1unlocker.desktop \
        -i AppDir/usr/share/icons/hicolor/256x256/apps/t1-unlocker.png \
        --exclude-library 'libudev.so*' \
        --plugin gtk"

# 5b) Il plugin-gtk re-invoca linuxdeploy internamente senza i nostri
#     --exclude-library e bundle-a anche libudev (più i suoi file copyright):
#     rimossa a mano prima del packaging. A runtime l'app userà la libudev
#     del sistema host, come richiesto dal vincolo sul daemon udev.
rm -f AppDir/usr/lib/libudev.so*
rm -rf AppDir/usr/share/doc/libudev1

# 5c) Packaging dell'AppImage: linuxdeploy riscansiona comunque l'AppDir e
#     ri-deployerebbe le dipendenze escluse, quindi il --exclude-library va
#     ripetuto anche qui; runtime e nome file forzati via
#     LDAI_RUNTIME_FILE/LDAI_OUTPUT
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src \
    -e APPIMAGE_EXTRACT_AND_RUN -e LINUXDEPLOY_OUTPUT_VERSION \
    -e LDAI_OUTPUT -e LDAI_RUNTIME_FILE \
    t1-unlocker-build \
    ./linuxdeploy.AppImage --appdir AppDir \
        --exclude-library 'libudev.so*' \
        -o appimage
ls -la "T1Unlocker-${VERSION}-x86_64.AppImage"
