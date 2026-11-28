return {
    onLaunch = function(p)
        local start, aim = p:position(), p:targetPoint()
        local dx, dy = aim.x - start.x, aim.y - start.y
        local distance = math.sqrt(dx * dx + dy * dy)
        if distance == 0 then
            p:detonate()
            return
        end
        local speed = p:weaponSpeed() / 256
        p:setVelocity(speed * dx / distance, speed * dy / distance, 0)
        return math.floor(p.life / 2) + 1
    end,

    onTick = function(p)
        local v = p:velocity()
        p:setVelocity(-v.x, -v.y, 0)
        return p.life
    end,
}
