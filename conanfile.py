from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout


class UcaNetConan(ConanFile):
    name = "uca-net"
    version = "1.0"
    license = "MIT"
    author = "Marius Elvert marius.elvert@softwareschneiderei.de"
    url = "https://github.com/ufo-kit/uca-net"
    description = "TCP-based network bridge for libuca."
    topics = ("utilities",)
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "with_zeromq": [True, False]}
    default_options = {"shared": True, "with_zeromq": False}
    generators = "CMakeDeps"
    exports_sources = "*.h", "*.c", "cmake/*", "CMakeLists.txt", "config.h.in"
    
    def configure(self):
        # These are not applicable for C libraries
        self.settings.rm_safe("compiler.libcxx")
        self.settings.rm_safe("compiler.cppstd")

    def requirements(self):
        self.requires("libuca/2.3.0")
        self.requires("glib/2.81.0")
        if self.options.with_zeromq:
            self.requires("zeromq/4.3.5")
            self.requires("json-c/0.18")

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["WITH_ZMQ_NETWORKING"] = (self.options.with_zeromq == True)
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

