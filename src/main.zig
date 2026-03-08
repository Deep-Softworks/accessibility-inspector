const std = @import("std");
const ax = @import("ax.zig");
const tree = @import("tree.zig");
const printer = @import("print.zig");

const default_depth: usize = 20;

pub fn main() void {
    run() catch |err| {
        if (err == error.InvalidArguments or
            err == error.AccessibilityPermissionRequired or
            err == error.AppNotFound or
            err == error.TreeUnavailable)
        {
            std.process.exit(1);
        }

        std.debug.print("error: {s}\n", .{@errorName(err)});
        std.process.exit(1);
    };
}

fn run() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();

    const allocator = gpa.allocator();
    const stderr = std.fs.File.stderr().deprecatedWriter();
    const stdout = std.fs.File.stdout().deprecatedWriter();

    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);

    if (args.len != 2) {
        try printUsage(stderr);
        return error.InvalidArguments;
    }

    const app_name = args[1];

    if (!ax.hasAccessibilityPermission()) {
        try stderr.writeAll(
            "Accessibility permission required.\n" ++
                "Enable it in:\n" ++
                "System Settings → Privacy & Security → Accessibility\n",
        );
        return error.AccessibilityPermissionRequired;
    }

    const pid = try resolveAppPid(allocator, app_name);
    const app = ax.createApplication(pid);
    defer ax.c.CFRelease(@ptrCast(app));

    var arena_state = std.heap.ArenaAllocator.init(allocator);
    defer arena_state.deinit();

    const root = try tree.buildTree(arena_state.allocator(), app, default_depth) orelse {
        try stderr.print("Unable to inspect accessibility tree for \"{s}\".\n", .{app_name});
        return error.TreeUnavailable;
    };

    try printer.printTree(stdout, root);
}

fn printUsage(writer: anytype) !void {
    try writer.writeAll("Usage: axtrace <app-name>\n");
}

fn resolveAppPid(allocator: std.mem.Allocator, app_name: []const u8) !ax.c.pid_t {
    if (try resolveWithPgrep(allocator, app_name)) |pid| {
        return pid;
    }

    if (try resolveWithPs(allocator, app_name)) |pid| {
        return pid;
    }

    const stderr = std.fs.File.stderr().deprecatedWriter();
    try stderr.print("Application \"{s}\" is not running.\n", .{app_name});
    return error.AppNotFound;
}

fn resolveWithPgrep(allocator: std.mem.Allocator, app_name: []const u8) !?ax.c.pid_t {
    const output = runCommand(allocator, &[_][]const u8{ "pgrep", "-ix", app_name }) catch return null;
    defer allocator.free(output);

    var lines = std.mem.tokenizeAny(u8, output, "\r\n");
    const first_line = lines.next() orelse return null;
    const pid_value = std.fmt.parseInt(i32, first_line, 10) catch return null;
    return @intCast(pid_value);
}

fn resolveWithPs(allocator: std.mem.Allocator, app_name: []const u8) !?ax.c.pid_t {
    const output = try runCommand(allocator, &[_][]const u8{ "ps", "-axo", "pid=,comm=" });
    defer allocator.free(output);

    var lines = std.mem.tokenizeScalar(u8, output, '\n');
    while (lines.next()) |line| {
        const trimmed = std.mem.trim(u8, line, " \t\r");
        if (trimmed.len == 0) {
            continue;
        }

        const first_space = std.mem.indexOfAny(u8, trimmed, " \t") orelse continue;
        const pid_text = std.mem.trim(u8, trimmed[0..first_space], " \t");
        const command_text = std.mem.trim(u8, trimmed[first_space + 1 ..], " \t");
        if (command_text.len == 0) {
            continue;
        }

        const process_name = extractProcessName(command_text);
        if (!std.ascii.eqlIgnoreCase(process_name, app_name)) {
            continue;
        }

        const pid_value = std.fmt.parseInt(i32, pid_text, 10) catch continue;
        return @intCast(pid_value);
    }

    return null;
}

fn extractProcessName(command_text: []const u8) []const u8 {
    if (std.mem.indexOf(u8, command_text, ".app/Contents/MacOS/")) |marker_index| {
        const bundle_path = command_text[0 .. marker_index + 4];
        return std.fs.path.stem(std.fs.path.basename(bundle_path));
    }

    return std.fs.path.stem(std.fs.path.basename(command_text));
}

fn runCommand(allocator: std.mem.Allocator, argv: []const []const u8) ![]u8 {
    const result = try std.process.Child.run(.{
        .allocator = allocator,
        .argv = argv,
    });
    defer allocator.free(result.stderr);

    switch (result.term) {
        .Exited => |code| {
            if (code != 0) {
                allocator.free(result.stdout);
                return error.CommandFailed;
            }
        },
        else => {
            allocator.free(result.stdout);
            return error.CommandFailed;
        },
    }

    return result.stdout;
}
