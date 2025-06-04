from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout


class UcaNetConan(ConanFile):
    name = "uca-net"
    version = "9906e95"
    license = "MIT"
    author = "Marius Elvert marius.elvert@softwareschneiderei.de"
    url = "https://github.com/ufo-kit/libuca"
    description = "TCP-based network bridge for libuca."
    topics = ("utilities",)
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False]}
    default_options = {"shared": True}
    generators = "CMakeDeps"
    exports_sources = "*.h", "*.c", "cmake/*", "CMakeLists.txt", "config.h.in"
    requires = "libuca/2.3.0", "glib/2.81.0",

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["WITH_ZMQ_NETWORKING"] = False # TODO, make this optional
        toolchain.variables["USE_FIND_PACKAGE_FOR_GLIB"] = True
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def layout(self):
        cmake_layout(self)

