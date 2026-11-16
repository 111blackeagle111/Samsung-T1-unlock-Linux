gtest_dep = dependency('gtest', main: true)
test_usb = executable('test_usb_io', 'test_usb_io.cpp',
    link_with: lib, include_directories: inc, dependencies: [gtest_dep])
test('usb_io', test_usb)
test_protocol = executable('test_protocol', 'test_protocol.cpp',
    link_with: lib, include_directories: inc, dependencies: [gtest_dep, usb_dep])
test('protocol', test_protocol)
test_detect = executable('test_detect', 'test_detect.cpp',
    link_with: lib, include_directories: inc, dependencies: [gtest_dep])
test('detect', test_detect)
