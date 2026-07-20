const std = @import("std");
const util = @import("util.zig");

pub const Point = struct {
    x: i32,
    y: i32,

    pub fn add(self: Point, other: Point) Point {
        return Point{ .x = self.x + other.x, .y = self.y + other.y };
    }
};

pub fn origin() Point {
    return Point{ .x = 0, .y = 0 };
}

pub fn main() void {
    const p = Point{ .x = 1, .y = 2 };
    const q = p.add(origin());
    _ = q;

    const v = util.Vec2{ .x = 3.0, .y = 4.0 };
    const scaled = util.scale(v, 2.0);
    const len = scaled.length();
    _ = len;
}
