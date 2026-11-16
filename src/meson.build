inc = include_directories('.')
lib = library('t1core', 'usb_io.cpp', 'protocol.cpp', 'block_dev_info.cpp',
    'mount_udisks.cpp',
    dependencies: [usb_dep, gudev_dep, gio_dep], include_directories: inc)
t1core_dep = declare_dependency(link_with: lib, include_directories: inc)
executable('t1-unlocker', 'main.cpp', 'mainwindow.cpp',
    link_with: lib, include_directories: inc,
    dependencies: [gtkmm_dep, usb_dep, thread_dep])  # usb_dep: LIBUSB_ERROR_ACCESS in mainwindow.cpp; thread_dep: std::thread
