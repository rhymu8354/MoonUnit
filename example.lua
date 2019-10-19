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
end)

moonunit:test("examples_failing", "buggy_abs_should_pass", function()
    moonunit:expect_eq(5, buggy_abs(-5))
end)

moonunit:test("examples_failing", "buggy_abs_should_fail_and_stop", function()
    moonunit:assert_eq(1, buggy_abs(-1))
    moonunit:expect_eq(1, buggy_abs(-1))
end)

moonunit:test("examples_failing", "buggy_abs_should_fail_but_continue", function()
    moonunit:expect_eq(1, buggy_abs(-1))
    moonunit:expect_eq(1, buggy_abs(-1))
end)
