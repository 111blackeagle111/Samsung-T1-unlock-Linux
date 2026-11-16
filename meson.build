project('t1-unlocker', 'cpp', version: '0.1.0',
        default_options: ['cpp_std=c++17', 'warning_level=2'])
gtkmm_dep = dependency('gtkmm-3.0')
gio_dep = dependency('giomm-2.4')  # D-Bus per mount_udisks; trascina gio-2.0
glib_dep = dependency('glib-2.0')
gudev_dep = dependency('gudev-1.0')
usb_dep = dependency('libusb-1.0')
thread_dep = dependency('threads')  # std::thread nel GUI (Task 6)
subdir('src')
if get_option('tests')
  subdir('tests')
endif
