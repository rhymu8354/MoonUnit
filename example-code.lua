-- This contains example Lua code unit-tested by MoonUnit.

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
