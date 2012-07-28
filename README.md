Example
========

####nginx.conf

    luap {
        lua_file conf/test.lua;
    }

####test.lua

    local i = 0
    local f = io.open("/tmp/luap", "w")

    function sleep(n)
        os.execute("sleep "..tonumber(n))
    end

    --ngx.signal("stop")

    sleep(10)
    r1,r2,r3,r4,r5,r6 = ngx.status()

    f:write(r1)
    f:write(r2)
    f:write(r3)
    f:write(r4)
    f:write(r5)
    f:write(r6)
    io.close(f)

