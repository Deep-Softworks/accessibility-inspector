const std = @import("std");
const tree = @import("tree.zig");

pub const Options = struct {
    show_values: bool = false,
};

pub fn printTree(writer: anytype, root: *const tree.Node, options: Options) !void {
    var guides = [_]bool{false} ** 32;

    try writer.print("{s}\n", .{root.role});
    try printAttributes(writer, root, &guides, 0, true, true, options);
    try printChildren(writer, root.first_child, &guides, 0, options);
}

fn printChildren(
    writer: anytype,
    child: ?*const tree.Node,
    guides: *[32]bool,
    depth: usize,
    options: Options,
) !void {
    var current = child;
    while (current) |node| {
        const is_last = node.next_sibling == null;
        try writeIndent(writer, guides, depth);
        try writer.print("{s}{s}\n", .{ if (is_last) "└─ " else "├─ ", node.role });
        try printAttributes(writer, node, guides, depth, is_last, false, options);

        guides[depth] = !is_last;
        try printChildren(writer, node.first_child, guides, depth + 1, options);
        current = node.next_sibling;
    }
}

fn printAttributes(
    writer: anytype,
    node: *const tree.Node,
    guides: *const [32]bool,
    depth: usize,
    is_last: bool,
    is_root: bool,
    options: Options,
) !void {
    if (options.show_values) {
        if (node.value) |value| {
            try writeAttributeIndent(writer, guides, depth, is_last, is_root);
            try writer.print("value=\"{s}\"\n", .{value});
        }
    }

    if (node.selected_range) |selected_range| {
        try writeAttributeIndent(writer, guides, depth, is_last, is_root);
        try writer.print("selectedRange={{{d},{d}}}\n", .{
            selected_range.location,
            selected_range.length,
        });
    }
}

fn writeIndent(writer: anytype, guides: *const [32]bool, depth: usize) !void {
    var index: usize = 0;
    while (index < depth) : (index += 1) {
        try writer.writeAll(if (guides[index]) "│   " else "    ");
    }
}

fn writeAttributeIndent(
    writer: anytype,
    guides: *const [32]bool,
    depth: usize,
    is_last: bool,
    is_root: bool,
) !void {
    if (is_root) {
        try writer.writeAll("    ");
        return;
    }

    try writeIndent(writer, guides, depth);
    try writer.writeAll(if (is_last) "    " else "│   ");
}
