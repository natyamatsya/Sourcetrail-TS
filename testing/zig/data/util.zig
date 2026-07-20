pub const Vec2 = struct {
    x: f64,
    y: f64,

    pub fn length(self: Vec2) f64 {
        return @sqrt(self.x * self.x + self.y * self.y);
    }
};

pub fn scale(v: Vec2, factor: f64) Vec2 {
    return Vec2{ .x = v.x * factor, .y = v.y * factor };
}
