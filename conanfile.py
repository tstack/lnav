from conans import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps


class LnavConan(ConanFile):
    name = "lnav"
    version = "0.12.1"
    homepage = "https://lnav.org"
    url = "https://github.com/tstack/lnav.git"
    license = "BSD-2-Clause"
    description = (
        "The Log File Navigator, lnav for short, is an advanced "
        "log file viewer for the small-scale"
    )
    settings = "os", "compiler", "build_type", "arch"
    exports_sources = "*"
    no_copy_source = True
    requires = (
        "bzip2/1.0.8",
        "libarchive/3.6.0",
        "libcurl/7.85.0",
        "ncurses/6.3",
        "pcre2/10.40",
        "readline/8.1.2",
        "sqlite3/3.38.0",
        "zlib/1.2.12",
    )
    generators = ("virtualrunenv",)
    default_options = {
        "libarchive:with_bzip2": True,
        "libarchive:with_lz4": True,
        "libarchive:with_lzo": True,
        "libarchive:with_lzma": True,
        "libarchive:with_zstd": True,
        "pcre2:support_jit": True,
        "pcre2:build_pcre2_8": True,
        "sqlite3:enable_json1": True,
        "sqlite3:enable_soundex": True,
        "readline:with_library": "curses",
    }

    def generate(self):
        CMakeToolchain(self).generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        if self.settings.os == "Macos" and self.settings.arch == "armv8":
            cmake.definitions["CMAKE_SYSTEM_PROCESSOR"] = "arm64"
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def deploy(self):
        self.copy("*", dst="bin", src="bin")
