local spacing = 1  -- cells between ground impacts

return {
    onLaunch = function(p)
        local start, aim = p:position(), p:targetPoint()
        local dx, dy = aim.x - start.x, aim.y - start.y
        local length = math.sqrt(dx * dx + dy * dy)
        if length == 0 then
            p:detonate()
            return
        end
        local speed = p:weaponSpeed() / 256
        p:setVelocity(speed * dx / length, speed * dy / length, 0)
        p.state.remainder = 0
    end,

    onTravel = function(p, from, to)
        local dx, dy = to.x - from.x, to.y - from.y
        local length = math.sqrt(dx * dx + dy * dy)
        if length == 0 then return end
        local distance = spacing - p.state.remainder
        while distance <= length do
            local t = distance / length
            local ground = p:groundPoint({x = from.x + dx * t, y = from.y + dy * t})
            p:action("DetonateWarhead", {Target = ground, Warhead = "HE", Amount = 40})
            distance = distance + spacing
        end
        p.state.remainder = length + spacing - distance
    end,
}
