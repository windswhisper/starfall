return {
    cast = function(ctx)
        for _, unit in ipairs(ctx:nearbyUnits(ctx.self, 4, {types={"FV", "ENGINEER"}})) do
            local pos = unit:position()
            local args = {Target=unit, Destination={x=pos.x+4, y=pos.y}, OpenHeight=3}
            assert(ctx:action("EjectUnit", args))
            assert(not ctx:action("EjectUnit", args))
        end
        return ctx:action("EjectPassengers", {Target=ctx.self, Destination=ctx.point, Interval=20, HorizontalSpeed=7, OpenHeight=2, Range=6})
    end,
}
