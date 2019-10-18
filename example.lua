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

-- moonunit:test("test_square_zero", function()
--     local x = 0
--     local y = square(x)
--     moonunit:expect_eq(0, y)
-- end)

function test_square_zero()
    local x = 0
    local y = square(x)
    moonunit:expect_eq(0, y)
end

function test_square_non_zero()
    local x = 5
    local y = square(x)
    moonunit:expect_eq(25, y)
end

function test_buggy_abs_should_pass()
    moonunit:expect_eq(5, buggy_abs(-5))
end

function test_buggy_abs_should_fail_and_stop()
    moonunit:assert_eq(1, buggy_abs(-1))
    moonunit:expect_eq(1, buggy_abs(-1))
end

function test_buggy_abs_should_fail_but_continue()
    moonunit:expect_eq(1, buggy_abs(-1))
    moonunit:expect_eq(1, buggy_abs(-1))
end
