const std = @import("std");

pub const c = @cImport({
    @cInclude("axtrace_ax.h");
});

pub const SelectedRange = struct {
    location: isize,
    length: isize,
};

// Attribute name constants - create CFStrings at runtime to avoid CFSTR macro
const ATTR_ROLE = "AXRole\x00";
const ATTR_VALUE = "AXValue\x00";
const ATTR_SELECTED_TEXT_RANGE = "AXSelectedTextRange\x00";
const ATTR_CHILDREN = "AXChildren\x00";
const ATTR_FOCUSED_UI_ELEMENT = "AXFocusedUIElement\x00";

var _attr_role: ?c.CFStringRef = null;
var _attr_value: ?c.CFStringRef = null;
var _attr_selected_range: ?c.CFStringRef = null;
var _attr_children: ?c.CFStringRef = null;
var _attr_focused_ui_element: ?c.CFStringRef = null;

fn attrRole() c.CFStringRef {
    if (_attr_role == null) {
        _attr_role = c.CFStringCreateWithCString(null, ATTR_ROLE.ptr, c.kCFStringEncodingUTF8);
    }
    return _attr_role.?;
}
fn attrValue() c.CFStringRef {
    if (_attr_value == null) {
        _attr_value = c.CFStringCreateWithCString(null, ATTR_VALUE.ptr, c.kCFStringEncodingUTF8);
    }
    return _attr_value.?;
}
fn attrSelectedTextRange() c.CFStringRef {
    if (_attr_selected_range == null) {
        _attr_selected_range = c.CFStringCreateWithCString(null, ATTR_SELECTED_TEXT_RANGE.ptr, c.kCFStringEncodingUTF8);
    }
    return _attr_selected_range.?;
}
fn attrChildren() c.CFStringRef {
    if (_attr_children == null) {
        _attr_children = c.CFStringCreateWithCString(null, ATTR_CHILDREN.ptr, c.kCFStringEncodingUTF8);
    }
    return _attr_children.?;
}
fn attrFocusedUIElement() c.CFStringRef {
    if (_attr_focused_ui_element == null) {
        _attr_focused_ui_element = c.CFStringCreateWithCString(null, ATTR_FOCUSED_UI_ELEMENT.ptr, c.kCFStringEncodingUTF8);
    }
    return _attr_focused_ui_element.?;
}

pub const AttributeNames = struct {
    array_ref: ?c.CFArrayRef = null,

    pub fn deinit(self: *const AttributeNames) void {
        if (self.array_ref) |array_ref| {
            c.CFRelease(@ptrCast(array_ref));
        }
    }

    pub fn containsRole(self: *const AttributeNames) bool {
        return self.contains(attrRole());
    }
    pub fn containsValue(self: *const AttributeNames) bool {
        return self.contains(attrValue());
    }
    pub fn containsSelectedTextRange(self: *const AttributeNames) bool {
        return self.contains(attrSelectedTextRange());
    }
    pub fn containsChildren(self: *const AttributeNames) bool {
        return self.contains(attrChildren());
    }

    fn contains(self: *const AttributeNames, attribute: c.CFStringRef) bool {
        const array_ref = self.array_ref orelse return false;
        const count = c.CFArrayGetCount(array_ref);

        var index: c.CFIndex = 0;
        while (index < count) : (index += 1) {
            const item = c.CFArrayGetValueAtIndex(array_ref, index);
            const name: c.CFStringRef = @ptrCast(@alignCast(item));
            if (c.CFStringCompare(name, attribute, 0) == c.kCFCompareEqualTo) {
                return true;
            }
        }

        return false;
    }
};

pub const Children = struct {
    array_ref: ?c.CFArrayRef = null,

    pub fn deinit(self: *const Children) void {
        if (self.array_ref) |array_ref| {
            c.CFRelease(@ptrCast(array_ref));
        }
    }

    pub fn len(self: *const Children) usize {
        const array_ref = self.array_ref orelse return 0;
        return @intCast(c.CFArrayGetCount(array_ref));
    }

    pub fn at(self: *const Children, index: usize) c.AXUIElementRef {
        const array_ref = self.array_ref.?;
        const item = c.CFArrayGetValueAtIndex(array_ref, @intCast(index));
        return @ptrCast(@alignCast(item));
    }
};

pub fn hasAccessibilityPermission() bool {
    return c.AXIsProcessTrusted() != 0;
}

pub fn createApplication(pid: c.pid_t) c.AXUIElementRef {
    return c.AXUIElementCreateApplication(pid);
}

pub fn copyAttributeNames(element: c.AXUIElementRef) AttributeNames {
    var names: c.CFArrayRef = null;
    const err = c.AXUIElementCopyAttributeNames(element, &names);
    if (err != c.kAXErrorSuccess or names == null) {
        return .{};
    }

    return .{ .array_ref = names };
}

pub fn getAttribute(element: c.AXUIElementRef, attribute: c.CFStringRef) ?c.CFTypeRef {
    var value: c.CFTypeRef = null;
    const err = c.AXUIElementCopyAttributeValue(element, attribute, &value);
    if (err != c.kAXErrorSuccess or value == null) {
        return null;
    }

    return value;
}

pub fn getChildren(element: c.AXUIElementRef) Children {
    var count: c.CFIndex = 0;
    const count_err = c.AXUIElementGetAttributeValueCount(element, attrChildren(), &count);
    if (count_err != c.kAXErrorSuccess or count <= 0) {
        return .{};
    }

    var values: c.CFArrayRef = null;
    const values_err = c.AXUIElementCopyAttributeValues(element, attrChildren(), 0, count, &values);
    if (values_err != c.kAXErrorSuccess or values == null) {
        return .{};
    }

    return .{ .array_ref = values };
}

pub fn getRole(allocator: std.mem.Allocator, element: c.AXUIElementRef) ?[]const u8 {
    const value = getAttribute(element, attrRole()) orelse return null;
    defer c.CFRelease(value);

    if (c.CFGetTypeID(value) != c.CFStringGetTypeID()) {
        return null;
    }

    return cfStringToOwned(allocator, @ptrCast(@alignCast(value)));
}

pub fn getValue(allocator: std.mem.Allocator, element: c.AXUIElementRef) ?[]const u8 {
    const value = getAttribute(element, attrValue()) orelse return null;
    defer c.CFRelease(value);

    return cfTypeToOwnedString(allocator, value);
}

pub fn getSelectedRange(element: c.AXUIElementRef) ?SelectedRange {
    const value = getAttribute(element, attrSelectedTextRange()) orelse return null;
    defer c.CFRelease(value);

    if (c.CFGetTypeID(value) != c.AXValueGetTypeID()) {
        return null;
    }

    const ax_value: c.AXValueRef = @ptrCast(@alignCast(@constCast(value)));
    if (c.AXValueGetType(ax_value) != c.kAXValueCFRangeType) {
        return null;
    }

    var range: c.CFRange = undefined;
    if (c.AXValueGetValue(ax_value, c.kAXValueCFRangeType, &range) == 0) {
        return null;
    }

    return .{
        .location = @intCast(range.location),
        .length = @intCast(range.length),
    };
}

pub fn getFocusedElement(element: c.AXUIElementRef) ?c.AXUIElementRef {
    const value_ref = getAttribute(element, attrFocusedUIElement()) orelse return null;
    const value = value_ref orelse return null;
    if (c.CFGetTypeID(value) != c.AXUIElementGetTypeID()) {
        c.CFRelease(value);
        return null;
    }

    // AXUIElementCopyAttributeValue returns a retained object we hand to caller.
    const focused: c.AXUIElementRef = @ptrCast(@alignCast(value));
    return focused;
}

fn cfStringToOwned(allocator: std.mem.Allocator, value: c.CFStringRef) ?[]const u8 {
    const length = c.CFStringGetLength(value);
    const max_size = c.CFStringGetMaximumSizeForEncoding(length, c.kCFStringEncodingUTF8);
    if (max_size < 0) {
        return null;
    }

    const buffer = allocator.alloc(u8, @intCast(max_size + 1)) catch return null;
    errdefer allocator.free(buffer);
    if (c.CFStringGetCString(value, buffer.ptr, @intCast(buffer.len), c.kCFStringEncodingUTF8) == 0) {
        return null;
    }

    const nul_index = std.mem.indexOfScalar(u8, buffer, 0) orelse buffer.len;
    return buffer[0..nul_index];
}

fn cfTypeToOwnedString(allocator: std.mem.Allocator, value: c.CFTypeRef) ?[]const u8 {
    const type_id = c.CFGetTypeID(value);

    if (type_id == c.CFStringGetTypeID()) {
        return cfStringToOwned(allocator, @ptrCast(@alignCast(value)));
    }

    if (type_id == c.CFBooleanGetTypeID()) {
        const bool_value: c.CFBooleanRef = @ptrCast(@alignCast(value));
        const flag = c.CFBooleanGetValue(bool_value) != 0;
        return std.fmt.allocPrint(allocator, "{}", .{flag}) catch null;
    }

    if (type_id == c.CFNumberGetTypeID()) {
        const number: c.CFNumberRef = @ptrCast(@alignCast(value));

        var integer_value: i64 = 0;
        if (c.CFNumberGetValue(number, c.kCFNumberLongLongType, &integer_value) != 0) {
            return std.fmt.allocPrint(allocator, "{}", .{integer_value}) catch null;
        }

        var float_value: f64 = 0;
        if (c.CFNumberGetValue(number, c.kCFNumberDoubleType, &float_value) != 0) {
            return std.fmt.allocPrint(allocator, "{}", .{float_value}) catch null;
        }
    }

    return null;
}
