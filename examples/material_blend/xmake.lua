set_project("libyafaray-material-blend")
set_xmakever("2.8.0")
set_allowedplats("mingw")
add_rules("mode.debug", "mode.release")

option("yafaray_prefix")
    set_default("../../build/install/tdm-gcc64-shared-opencv")
    set_showmenu(true)
    set_description("Installation prefix of the shared libYafaRay library")
option_end()

target("yafaray_material_blend")
    set_kind("binary")
    set_languages("c90")
    set_warnings("all", "extra")
    add_files("material_blend.c")
    set_rundir("build")
    on_load(function (target)
        local prefix = path.absolute(get_config("yafaray_prefix"), os.projectdir())
        for _, header in ipairs({"yafaray_c_api.h", "yafaray_c_api_export.h"}) do
            assert(os.isfile(path.join(prefix, "include", header)),
                   "Install libYafaRay first or set --yafaray_prefix=<installation prefix>")
        end
        assert(os.isfile(path.join(prefix, "lib", "liblibyafaray4.dll.a")) and
               os.isfile(path.join(prefix, "bin", "libyafaray4.dll")),
               "Expected an installed MinGW shared library in " .. prefix)
        target:add("includedirs", path.join(prefix, "include"))
        target:add("linkdirs", path.join(prefix, "lib"))
        target:add("links", "libyafaray4")
        target:add("runenvs", "PATH", path.join(prefix, "bin"))
    end)
