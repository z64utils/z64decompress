const std = @import("std");

pub fn build(b: *std.Build) !void {
    const strip = b.option(bool, "strip", "remove debug symbols from executable") orelse false;

    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "z64decompress",
        .target = target,
        .optimize = optimize,
        .strip = strip,
    });

    exe.addCSourceFiles(.{
        .files = &.{
            "src/main.c",
            "src/file.c",
            "src/n64crc.c",
            "src/wow.c",
            "src/decoder/aplib.c",
            "src/decoder/lzo.c",
            "src/decoder/ucl.c",
            "src/decoder/yaz.c",
            "src/decoder/zlib.c",
        },
    });

    exe.linkLibC();

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}