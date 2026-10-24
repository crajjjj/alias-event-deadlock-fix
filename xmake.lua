set_xmakever("3.0.0")  -- the commonlibsse-ng checkout requires 3.0

-- Globals
PROJECT_NAME = "AliasEventDeadlockFix"
PROJECT_VERSION = "0.1.1"
PROJECT_AUTHOR = "crajjjj"

set_project(PROJECT_NAME)
set_version(PROJECT_VERSION)
set_languages("cxx23")
set_license("gplv3")
set_warnings("allextra")

option("copy_to_mod")
    set_default(false)
    set_description("Copy dist/* to <XSE_TES5_MODS_PATH>/" .. PROJECT_NAME)
option_end()

-- CommonLibSSE-NG (alandtse fork, v7.1.0+). Not part of this repository: clone it to
-- lib/commonlibsse-ng (or add it as a submodule) before building.
includes("lib/commonlibsse-ng")

add_rules("mode.debug", "mode.release")

if is_mode("debug") then
    add_defines("DEBUG")
    set_optimize("none")
    set_runtimes("MTd")
elseif is_mode("release") then
    add_defines("NDEBUG")
    set_optimize("fastest")
    set_symbols("debug")
    set_runtimes("MT")
end

add_defines("_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING")
add_defines("NOMINMAX")  -- CommonLib v7 headers pull in Windows.h

target(PROJECT_NAME)
    set_kind("shared")
    add_deps("commonlibsse-ng")
    add_rules("commonlibsse-ng.plugin", {
        name = PROJECT_NAME,
        author = PROJECT_AUTHOR,
        description = "Breaks the alias-fill / event-relay lock inversion on an actor's extra data (hard freeze, black screen with HUD)."
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/PCH.h")

    add_cxxflags(
        "cl::/diagnostics:caret",
        "cl::/wd4200",
        "cl::/wd4201",
        "cl::/Zc:preprocessor",
        "cl::/utf-8"
    )

    after_build(function (target)
        local plugin_folder = path.join(os.projectdir(), "dist", "SKSE", "Plugins")
        if not os.isdir(plugin_folder) then
            os.mkdir(plugin_folder)
        end
        os.cp(target:targetfile(), plugin_folder)
        local pdb = target:symbolfile()
        if pdb and os.isfile(pdb) then
            os.cp(pdb, plugin_folder)
        end
        local mod_folder = os.getenv("XSE_TES5_MODS_PATH")
        if mod_folder and has_config("copy_to_mod") then
            os.cp("dist/*", path.join(mod_folder, PROJECT_NAME))
        end
    end)
target_end()
