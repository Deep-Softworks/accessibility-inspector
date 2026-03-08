const std = @import("std");
const ax = @import("ax.zig");

pub const Node = struct {
    role: []const u8,
    value: ?[]const u8 = null,
    selected_range: ?ax.SelectedRange = null,
    first_child: ?*Node = null,
    next_sibling: ?*Node = null,
};

pub fn buildTree(
    arena: std.mem.Allocator,
    element: ax.c.AXUIElementRef,
    max_depth: usize,
) !?*Node {
    return buildNode(arena, element, 0, max_depth);
}

fn buildNode(
    arena: std.mem.Allocator,
    element: ax.c.AXUIElementRef,
    depth: usize,
    max_depth: usize,
) !?*Node {
    if (depth > max_depth) {
        return null;
    }

    const attributes = ax.copyAttributeNames(element);
    defer attributes.deinit();

    if (!attributes.containsRole()) {
        return null;
    }

    const role = ax.getRole(arena, element) orelse return null;

    const node = try arena.create(Node);
    node.* = .{
        .role = role,
    };

    if (attributes.containsValue()) {
        node.value = ax.getValue(arena, element);
    }

    if (attributes.containsSelectedTextRange()) {
        node.selected_range = ax.getSelectedRange(element);
    }

    if (depth >= max_depth) {
        return node;
    }

    if (!attributes.containsChildren()) {
        return node;
    }

    const children = ax.getChildren(element);
    defer children.deinit();

    var previous_child: ?*Node = null;
    var index: usize = 0;
    while (index < children.len()) : (index += 1) {
        const child = try buildNode(arena, children.at(index), depth + 1, max_depth);
        if (child) |child_node| {
            if (node.first_child == null) {
                node.first_child = child_node;
            } else {
                previous_child.?.next_sibling = child_node;
            }
            previous_child = child_node;
        }
    }

    return node;
}
