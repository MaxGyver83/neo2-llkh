import qbs

Application {
    consoleApplication: false

    files: [
        "main.cpp",
        "trayicon.cpp",
        "trayicon.h",
    ]
    Group {
        name: "Resources"
        files: [
            "appicon-disabled.ico",
            "appicon.ico",
            "resources.h",
            "resources.rc",
        ]
    }

    Depends { name: "cpp" }
    cpp.cLanguageVersion: "c11"
    cpp.minimumWindowsVersion: "5.0"
    cpp.windowsApiCharacterSet: "unicode"
    cpp.cxxLanguageVersion: "c++17"
    cpp.enableExceptions: false
    cpp.dynamicLibraries: ["User32", "Shell32"]
    cpp.runtimeLibrary: "static"
    cpp.cxxFlags: ["/utf-8"]

    qbs.install: true
}
