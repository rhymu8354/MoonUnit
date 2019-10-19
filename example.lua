-- This is an example Lua script with unit tests that can be located
-- and executed by MoonUnit.

function square(x)
    return x * x;
end

function buggy_abs(x)
    if x < -1 then
        return -x
    else
        return x
    end
end

if not moonunit then return end

moonunit:test("examples_passing", "square_zero", function()
    local x = 0
    local y = square(x)
    moonunit:expect_eq(0, y)
end)

moonunit:test("examples_passing", "square_non_zero", function()
    local x = 5
    local y = square(x)
    moonunit:expect_eq(25, y)
    moonunit:assert_eq(25, y)
    moonunit:expect_ne(42, y)
    moonunit:assert_ne(42, y)
    moonunit:expect_true(y == 25)
    moonunit:assert_true(y == 25)
    moonunit:expect_false(y ~= 25)
    moonunit:assert_false(y ~= 25)
end)

moonunit:test("examples_passing", "square_order", function()
    local x = 5
    local y = square(x)
    moonunit:expect_gt(y, 24)
    moonunit:assert_gt(y, 24)
    moonunit:expect_ge(y, 25)
    moonunit:assert_ge(y, 25)
    moonunit:expect_lt(y, 26)
    moonunit:assert_lt(y, 26)
    moonunit:expect_le(y, 25)
    moonunit:assert_le(y, 25)
end)

moonunit:test("examples_passing", "buggy_abs_should_pass", function()
    moonunit:expect_eq(5, buggy_abs(-5))
end)

moonunit:test("examples_failing", "square_non_zero", function()
    local x = 5
    local y = square(x)
    moonunit:expect_eq(42, y)
    moonunit:expect_ne(25, y)
    moonunit:expect_true(y ~= 25)
    moonunit:expect_false(y == 25)
end)

moonunit:test("examples_failing", "square_non_zero_eq", function()
    local x = 5
    local y = square(x)
    moonunit:assert_eq(42, y)
    moonunit:assert_true(false)
    moonunit:expect_ne(25, y)
    moonunit:expect_true(y ~= 25)
    moonunit:expect_false(y == 25)
end)

moonunit:test("examples_failing", "square_non_zero_ne", function()
    local x = 5
    local y = square(x)
    moonunit:assert_ne(25, y)
    moonunit:assert_true(false)
end)

moonunit:test("examples_failing", "square_non_zero_true", function()
    local x = 5
    local y = square(x)
    moonunit:assert_true(y ~= 25)
    moonunit:assert_true(false)
end)

moonunit:test("examples_failing", "square_non_zero_false", function()
    local x = 5
    local y = square(x)
    moonunit:assert_false(y == 25)
    moonunit:assert_true(false)
end)

moonunit:test("examples_failing", "square_order", function()
    local x = 5
    local y = square(x)
    moonunit:expect_gt(y, 25)
    moonunit:expect_ge(y, 26)
    moonunit:expect_lt(y, 25)
    moonunit:expect_le(y, 24)
end)

moonunit:test("examples_failing", "square_order_gt", function()
    local x = 5
    local y = square(x)
    moonunit:assert_gt(y, 25)
    moonunit:assert_true(false)
end)

moonunit:test("examples_failing", "square_order_ge", function()
    local x = 5
    local y = square(x)
    moonunit:assert_ge(y, 26)
    moonunit:assert_true(false)
end)

moonunit:test("examples_failing", "square_order_lt", function()
    local x = 5
    local y = square(x)
    moonunit:assert_lt(y, 25)
    moonunit:assert_true(false)
end)

moonunit:test("examples_failing", "square_order_le", function()
    local x = 5
    local y = square(x)
    moonunit:assert_le(y, 24)
    moonunit:assert_true(false)
end)

moonunit:test("examples_failing", "buggy_abs_should_fail_and_stop", function()
    moonunit:assert_eq(1, buggy_abs(-1))
    moonunit:expect_eq(1, buggy_abs(-1))
end)

moonunit:test("examples_failing", "buggy_abs_should_fail_but_continue", function()
    moonunit:expect_eq(1, buggy_abs(-1))
    moonunit:expect_eq(1, buggy_abs(-1))
end)
