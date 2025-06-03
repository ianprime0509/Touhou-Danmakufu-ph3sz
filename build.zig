const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{
        .default_target = .{
            .cpu_arch = .x86,
            .cpu_features_add = std.Target.x86.featureSet(&.{
                std.Target.x86.Feature.sse,
                std.Target.x86.Feature.sse2,
                std.Target.x86.Feature.sse3,
                std.Target.x86.Feature.sse4_1,
                std.Target.x86.Feature.sse4_2,
            }),
            .os_tag = .windows,
            .os_version_min = .{ .windows = .win7 },
        },
    });
    const optimize = b.standardOptimizeOption(.{});

    const zlib = b.dependency("zlib", .{
        .target = target,
        .optimize = optimize,
    }).artifact("z");

    const ogg_dep = b.dependency("ogg", .{});
    const ogg_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    const ogg_configtypes = b.addConfigHeader(.{
        .style = .{ .cmake = ogg_dep.path("include/ogg/config_types.h.in") },
        .include_path = "ogg/config_types.h",
    }, .{
        .INCLUDE_INTTYPES_H = false,
        .INCLUDE_STDINT_H = true,
        .INCLUDE_SYS_TYPES_H = false,
        .SIZE16 = "int16_t",
        .USIZE16 = "uint16_t",
        .SIZE32 = "int32_t",
        .USIZE32 = "uint32_t",
        .SIZE64 = "int64_t",
        .USIZE64 = "uint64_t",
    });
    ogg_mod.addIncludePath(ogg_dep.path("include"));
    ogg_mod.addConfigHeader(ogg_configtypes);
    ogg_mod.addCSourceFiles(.{
        .root = ogg_dep.path("src"),
        .files = &.{
            "bitwise.c",
            "framing.c",
        },
    });
    const ogg = b.addLibrary(.{
        .name = "ogg",
        .root_module = ogg_mod,
    });
    ogg.installHeadersDirectory(ogg_dep.path("include/ogg"), "ogg", .{});
    ogg.installConfigHeader(ogg_configtypes);

    const vorbis_dep = b.dependency("vorbis", .{});
    const vorbis_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    vorbis_mod.linkLibrary(ogg);
    vorbis_mod.addIncludePath(vorbis_dep.path("include"));
    vorbis_mod.addIncludePath(vorbis_dep.path("lib"));
    vorbis_mod.addCSourceFiles(.{
        .root = vorbis_dep.path("lib"),
        .files = &.{
            "mdct.c",
            "smallft.c",
            "block.c",
            "envelope.c",
            "window.c",
            "lsp.c",
            "lpc.c",
            "analysis.c",
            "synthesis.c",
            "psy.c",
            "info.c",
            "floor1.c",
            "floor0.c",
            "res0.c",
            "mapping0.c",
            "registry.c",
            "codebook.c",
            "sharedbook.c",
            "lookup.c",
            "bitrate.c",
        },
    });
    const vorbis = b.addLibrary(.{
        .name = "vorbis",
        .root_module = vorbis_mod,
    });
    vorbis.installHeadersDirectory(vorbis_dep.path("include/vorbis"), "vorbis", .{});

    const vorbisenc_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    vorbisenc_mod.linkLibrary(ogg);
    vorbisenc_mod.linkLibrary(vorbis);
    vorbisenc_mod.addIncludePath(vorbis_dep.path("include"));
    vorbisenc_mod.addIncludePath(vorbis_dep.path("lib"));
    vorbisenc_mod.addCSourceFiles(.{
        .root = vorbis_dep.path("lib"),
        .files = &.{
            "vorbisenc.c",
        },
    });
    const vorbisenc = b.addLibrary(.{
        .name = "vorbisenc",
        .root_module = vorbisenc_mod,
    });

    const vorbisfile_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    vorbisfile_mod.linkLibrary(ogg);
    vorbisfile_mod.linkLibrary(vorbis);
    vorbisfile_mod.addIncludePath(vorbis_dep.path("include"));
    vorbisfile_mod.addIncludePath(vorbis_dep.path("lib"));
    vorbisfile_mod.addCSourceFiles(.{
        .root = vorbis_dep.path("lib"),
        .files = &.{
            "vorbisfile.c",
        },
    });
    const vorbisfile = b.addLibrary(.{
        .name = "vorbisfile",
        .root_module = vorbisfile_mod,
    });

    const kissfft_dep = b.dependency("kissfft", .{});
    const kissfft_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    kissfft_mod.addCSourceFiles(.{
        .root = kissfft_dep.path("."),
        .files = &.{
            "kiss_fft.c",
            "kfc.c",
            "kiss_fftnd.c",
            "kiss_fftndr.c",
            "kiss_fftr.c",
        },
        .flags = &.{
            "-ffast-math",
            "-fomit-frame-pointer",
            "-W",
            "-Wall",
            "-Wcast-align",
            "-Wcast-qual",
            "-Wshadow",
            "-Wwrite-strings",
            "-Wstrict-prototypes",
            "-Wmissing-prototypes",
            "-Wnested-externs",
            "-Wbad-function-cast",
        },
    });
    const kissfft = b.addLibrary(.{
        .name = "kissfft",
        .root_module = kissfft_mod,
    });
    kissfft.installHeadersDirectory(kissfft_dep.path("."), "kissfft", .{
        .include_extensions = &.{
            "kiss_fft.h",
            "kissfft.hh",
            "kiss_fftnd.h",
            "kiss_fftndr.h",
            "kiss_fftr.h",
        },
    });

    const imgui_dep = b.dependency("imgui", .{});
    const imgui_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libcpp = true,
    });
    imgui_mod.addCMacro("_UNICODE", "");
    imgui_mod.addCMacro("UNICODE", "");
    const imgui_defines: []const []const u8 = &.{
        //"IMGUI_DISABLE_OBSOLETE_FUNCTIONS", // CalcListClipping obsoleted in 1.86
        "IMGUI_DISABLE_OBSOLETE_KEYIO",
        "IMGUI_USE_BGRA_PACKED_COLOR",
        "IMGUI_USE_WCHAR32",
    };
    for (imgui_defines) |imgui_define| {
        imgui_mod.addCMacro(imgui_define, "");
    }
    const imgui_include_dirs: []const []const u8 = &.{
        ".",
        "misc/cpp",
        "misc/freetype",
        "misc/single_file",
        "backends",
    };
    for (imgui_include_dirs) |imgui_include_dir| {
        imgui_mod.addIncludePath(imgui_dep.path(imgui_include_dir));
    }
    imgui_mod.addCSourceFiles(.{
        .root = imgui_dep.path("."),
        .files = &.{
            "imgui.cpp",
            "imgui_draw.cpp",
            "imgui_tables.cpp",
            "imgui_widgets.cpp",
            "imgui_demo.cpp",
            "misc/cpp/imgui_stdlib.cpp",
            "backends/imgui_impl_win32.cpp",
            "backends/imgui_impl_dx9.cpp",
        },
    });
    const imgui = b.addLibrary(.{
        .name = "imgui",
        .root_module = imgui_mod,
    });
    for (imgui_include_dirs) |imgui_include_dir| {
        imgui.installHeadersDirectory(imgui_dep.path(imgui_include_dir), "", .{});
    }

    const dnh_cflags: []const []const u8 = &.{
        "-std=c++23",
        "-Wno-c++11-narrowing",
        "-Wno-deprecated-enum-float-conversion",
        "-Wno-deprecated-volatile",
        "-Wno-format-security",
        "-Wno-parentheses",
        "-Wno-potentially-evaluated-expression",
        "-Wno-switch",
        "-Wno-switch-enum",
        "-Wno-uninitialized",
        "-Wno-writable-strings",
    };
    // To help in debugging, applications are built using the console subsystem
    // in debug mode, so that stdout can be used for "printf debugging".
    const dnh_subsystem: std.Target.SubSystem = if (optimize == .Debug) .Console else .Windows;

    const dnh_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    dnh_mod.linkSystemLibrary("comctl32", .{});
    dnh_mod.linkSystemLibrary("d3d9", .{});
    dnh_mod.linkSystemLibrary("d3dx9_43", .{});
    dnh_mod.linkSystemLibrary("d3dcompiler_43", .{});
    dnh_mod.linkSystemLibrary("dinput8", .{});
    dnh_mod.linkSystemLibrary("dsound", .{});
    dnh_mod.linkSystemLibrary("dwmapi", .{});
    dnh_mod.linkSystemLibrary("gdi32", .{});
    dnh_mod.linkSystemLibrary("ole32", .{});
    dnh_mod.linkSystemLibrary("oleaut32", .{});
    dnh_mod.linkSystemLibrary("pdh", .{});
    dnh_mod.linkLibrary(zlib);
    dnh_mod.linkLibrary(ogg);
    dnh_mod.linkLibrary(vorbis);
    dnh_mod.linkLibrary(vorbisenc);
    dnh_mod.linkLibrary(vorbisfile);
    dnh_mod.linkLibrary(kissfft);
    dnh_mod.linkLibrary(imgui);
    for (imgui_defines) |imgui_define| {
        dnh_mod.addCMacro(imgui_define, "");
    }
    dnh_mod.addCMacro("DNH_PROJ_EXECUTOR", "");
    dnh_mod.addIncludePath(b.path("."));
    dnh_mod.addIncludePath(b.path("source/GcLib"));
    dnh_mod.addCSourceFiles(.{
        .root = b.path("source"),
        .files = &.{
            "GcLib/pch.cpp",
            "GcLib/gstd/Application.cpp",
            "GcLib/gstd/ArchiveFile.cpp",
            "GcLib/gstd/CompressorStream.cpp",
            "GcLib/gstd/CpuInformation.cpp",
            "GcLib/gstd/File.cpp",
            "GcLib/gstd/FpsController.cpp",
            "GcLib/gstd/GstdUtility.cpp",
            "GcLib/gstd/Logger.cpp",
            "GcLib/gstd/RandProvider.cpp",
            "GcLib/gstd/ScriptClient.cpp",
            "GcLib/gstd/Task.cpp",
            "GcLib/gstd/Thread.cpp",
            "GcLib/gstd/Window.cpp",
            "GcLib/gstd/Script/Parser.cpp",
            "GcLib/gstd/Script/Script.cpp",
            "GcLib/gstd/Script/ScriptFunction.cpp",
            "GcLib/gstd/Script/ScriptLexer.cpp",
            "GcLib/gstd/Script/Value.cpp",
            "GcLib/gstd/Script/ValueVector.cpp",
            "GcLib/directx/DirectGraphicsBase.cpp",
            "GcLib/directx/DirectGraphics.cpp",
            "GcLib/directx/DirectInput.cpp",
            "GcLib/directx/DirectSound.cpp",
            "GcLib/directx/DxCamera.cpp",
            "GcLib/directx/DxObject.cpp",
            "GcLib/directx/DxScriptObjClone.cpp",
            "GcLib/directx/DxScript.cpp",
            "GcLib/directx/DxText.cpp",
            "GcLib/directx/DxUtility.cpp",
            "GcLib/directx/DxUtilityIntersection.cpp",
            "GcLib/directx/DxWindow.cpp",
            "GcLib/directx/HLSL.cpp",
            "GcLib/directx/ImGuiWindow.cpp",
            "GcLib/directx/MetasequoiaMesh.cpp",
            "GcLib/directx/RenderObject.cpp",
            "GcLib/directx/ScriptManager.cpp",
            "GcLib/directx/Shader.cpp",
            "GcLib/directx/SystemPanel.cpp",
            "GcLib/directx/Texture.cpp",
            "GcLib/directx/TransitionEffect.cpp",
            "GcLib/directx/VertexBuffer.cpp",
            "TouhouDanmakufu/Common/DnhCommon.cpp",
            "TouhouDanmakufu/Common/DnhGcLibImpl.cpp",
            "TouhouDanmakufu/Common/DnhReplay.cpp",
            "TouhouDanmakufu/Common/DnhScript.cpp",
            "TouhouDanmakufu/Common/StgCommon.cpp",
            "TouhouDanmakufu/Common/StgCommonData.cpp",
            "TouhouDanmakufu/Common/StgControlScript.cpp",
            "TouhouDanmakufu/Common/StgEnemy.cpp",
            "TouhouDanmakufu/Common/StgIntersection.cpp",
            "TouhouDanmakufu/Common/StgItem.cpp",
            "TouhouDanmakufu/Common/StgPackageController.cpp",
            "TouhouDanmakufu/Common/StgPackageScript.cpp",
            "TouhouDanmakufu/Common/StgPlayer.cpp",
            "TouhouDanmakufu/Common/StgShot.cpp",
            "TouhouDanmakufu/Common/StgStageController.cpp",
            "TouhouDanmakufu/Common/StgStageScript.cpp",
            "TouhouDanmakufu/Common/StgSystem.cpp",
            "TouhouDanmakufu/Common/StgUserExtendScene.cpp",
            "TouhouDanmakufu/Common/DnhConfiguration.cpp",
            "TouhouDanmakufu/DnhExecutor/Common.cpp",
            "TouhouDanmakufu/DnhExecutor/GcLibImpl.cpp",
            "TouhouDanmakufu/DnhExecutor/ScriptSelectScene.cpp",
            "TouhouDanmakufu/DnhExecutor/StgScene.cpp",
            "TouhouDanmakufu/DnhExecutor/System.cpp",
            "TouhouDanmakufu/DnhExecutor/TitleScene.cpp",
            "TouhouDanmakufu/DnhExecutor/WinMain.cpp",
        },
        .flags = dnh_cflags,
    });
    dnh_mod.addWin32ResourceFile(.{
        .file = b.path("source/TouhouDanmakufu/DnhExecutor/DnhExecuter.rc"),
    });
    const dnh = b.addExecutable(.{
        .name = "th_dnh_ph3sz",
        .root_module = dnh_mod,
    });
    dnh.subsystem = dnh_subsystem;
    dnh.mingw_unicode_entry_point = true;
    b.installArtifact(dnh);

    const dnh_config_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    dnh_config_mod.linkSystemLibrary("comctl32", .{});
    dnh_config_mod.linkSystemLibrary("d3d9", .{});
    dnh_config_mod.linkSystemLibrary("dinput8", .{});
    dnh_config_mod.linkSystemLibrary("dwmapi", .{});
    dnh_config_mod.linkSystemLibrary("gdi32", .{});
    dnh_config_mod.linkSystemLibrary("ole32", .{});
    dnh_config_mod.linkLibrary(imgui);
    for (imgui_defines) |imgui_define| {
        dnh_config_mod.addCMacro(imgui_define, "");
    }
    dnh_config_mod.addCMacro("DNH_PROJ_CONFIG", "");
    dnh_config_mod.addIncludePath(b.path("."));
    dnh_config_mod.addIncludePath(b.path("source/GcLib"));
    dnh_config_mod.addCSourceFiles(.{
        .root = b.path("source"),
        .files = &.{
            "GcLib/pch.cpp",
            "GcLib/gstd/Application.cpp",
            "GcLib/gstd/CpuInformation.cpp",
            "GcLib/gstd/File.cpp",
            "GcLib/gstd/GstdUtility.cpp",
            "GcLib/gstd/Logger.cpp",
            "GcLib/gstd/Thread.cpp",
            "GcLib/gstd/Window.cpp",
            "GcLib/directx/DirectGraphicsBase.cpp",
            "GcLib/directx/DirectInput.cpp",
            "GcLib/directx/ImGuiWindow.cpp",
            "TouhouDanmakufu/Common/DnhCommon.cpp",
            "TouhouDanmakufu/Common/DnhGcLibImpl.cpp",
            "TouhouDanmakufu/Common/DnhConfiguration.cpp",
            "TouhouDanmakufu/DnhConfig/Common.cpp",
            "TouhouDanmakufu/DnhConfig/ControllerMap.cpp",
            "TouhouDanmakufu/DnhConfig/GcLibImpl.cpp",
            "TouhouDanmakufu/DnhConfig/MainWindow.cpp",
            "TouhouDanmakufu/DnhConfig/WinMain.cpp",
        },
        .flags = dnh_cflags,
    });
    dnh_config_mod.addWin32ResourceFile(.{
        .file = b.path("source/TouhouDanmakufu/DnhConfig/DnhConfig.rc"),
    });
    const dnh_config = b.addExecutable(.{
        .name = "config_ph3sz",
        .root_module = dnh_config_mod,
    });
    dnh_config.subsystem = dnh_subsystem;
    dnh_config.mingw_unicode_entry_point = true;
    b.installArtifact(dnh_config);

    const dnh_archiver_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    dnh_archiver_mod.linkSystemLibrary("comctl32", .{});
    dnh_archiver_mod.linkSystemLibrary("d3d9", .{});
    dnh_archiver_mod.linkSystemLibrary("dwmapi", .{});
    dnh_archiver_mod.linkSystemLibrary("gdi32", .{});
    dnh_archiver_mod.linkSystemLibrary("ole32", .{});
    dnh_archiver_mod.linkLibrary(zlib);
    dnh_archiver_mod.linkLibrary(imgui);
    for (imgui_defines) |imgui_define| {
        dnh_archiver_mod.addCMacro(imgui_define, "");
    }
    dnh_archiver_mod.addCMacro("DNH_PROJ_FILEARCHIVER", "");
    dnh_archiver_mod.addIncludePath(b.path("."));
    dnh_archiver_mod.addIncludePath(b.path("source/GcLib"));
    dnh_archiver_mod.addCSourceFiles(.{
        .root = b.path("source"),
        .files = &.{
            "GcLib/pch.cpp",
            "GcLib/directx/DirectGraphicsBase.cpp",
            "GcLib/directx/ImGuiWindow.cpp",
            "GcLib/gstd/Application.cpp",
            "GcLib/gstd/ArchiveFile.cpp",
            "GcLib/gstd/CompressorStream.cpp",
            "GcLib/gstd/CpuInformation.cpp",
            "GcLib/gstd/File.cpp",
            "GcLib/gstd/GstdUtility.cpp",
            "GcLib/gstd/Logger.cpp",
            "GcLib/gstd/Thread.cpp",
            "GcLib/gstd/Window.cpp",
            "FileArchiver/LibImpl.cpp",
            "FileArchiver/MainWindow.cpp",
            "FileArchiver/WinMain.cpp",
        },
        .flags = dnh_cflags,
    });
    dnh_archiver_mod.addWin32ResourceFile(.{
        .file = b.path("source/FileArchiver/FileArchiver.rc"),
    });
    const dnh_archiver = b.addExecutable(.{
        .name = "archiver_ph3sz",
        .root_module = dnh_archiver_mod,
    });
    dnh_archiver.subsystem = dnh_subsystem;
    dnh_archiver.mingw_unicode_entry_point = true;
    b.installArtifact(dnh_archiver);
}
