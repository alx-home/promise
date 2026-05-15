import os

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import collect_libs, copy, save


class AlxPromiseConan(ConanFile):
    name = "alx-promise"
    version = "1.6.0"
    license = "MIT"
    author = "alx-home"
    url = "https://github.com/alx-home/promise"
    description = "A lightweight C++ promise library."
    topics = ("cpp", "promise", "async", "alx", "alx-home")

    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    exports_sources = "CMakeLists.txt", "include/*", "src/*", "module/*", "LICENSE", "README.md", "test/*"

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def requirements(self):
        self.requires("alx-cpp-utils/1.1.0")

    def build_requirements(self):
        self.tool_requires("alx-build-tools/1.1.0")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        build_tools_dep = self.dependencies.build["alx-build-tools"]
        build_tools_dir = build_tools_dep.package_folder.replace("\\", "/")
        project_include = os.path.join(self.generators_folder, "project_include.cmake").replace("\\", "/")
        save(
            self,
            project_include,
            "\n".join(
                [
                    f'include("{build_tools_dir}/cmake/win32_library.cmake")',
                    f'include("{build_tools_dir}/cmake/win32_executable.cmake")',
                    "if(NOT TARGET alx-home::cpp_utils)",
                    "    find_package(alx-cpp-utils CONFIG REQUIRED)",
                    "endif()",
                ]
            ),
        )

        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.variables["CMAKE_PROJECT_INCLUDE"] = project_include
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        tc.variables["PROMISE_BUILD_TESTS"] = False
        tc.variables["PROMISE_FETCH_BUILD_TOOLS"] = False
        tc.variables["PROMISE_FETCH_CPP_UTILS"] = False
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h", src=os.path.join(self.source_folder, "include"), dst=os.path.join(self.package_folder, "include"))
        copy(self, "*.inl", src=os.path.join(self.source_folder, "include"), dst=os.path.join(self.package_folder, "include"))

        copy(self, "*.lib", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.so*", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dylib", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dll", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"), keep_path=False)

        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "share", self.name))
        copy(self, "README.md", src=self.source_folder, dst=os.path.join(self.package_folder, "share", self.name))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "alx-promise")
        self.cpp_info.set_property("cmake_target_name", "alx-home::promise")
        self.cpp_info.libs = collect_libs(self)
        self.cpp_info.requires = ["alx-cpp-utils::alx-cpp-utils"]