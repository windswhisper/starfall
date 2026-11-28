return {
    probeGround = function(p)
        local hill = p:groundPoint({x=66.25, y=84, z=10})
        local bridge = p:groundPoint({x=67.25, y=84, z=10})
        assert(hill.x == 66.25 and bridge.x == 67.25)
        if p.state.hill then
            assert(hill.z > p.state.hill and bridge.z > p.state.bridge)
        end
        p.state.hill, p.state.bridge = hill.z, bridge.z
    end,
    onLaunch = function(p)
        assert(p.age == 0 and p.life == 30 and p:launchSpeed() >= 0)
        p.state.speed = p:weaponSpeed()
        if p.state.speed == 24 then
            p:setVelocity(0, 0, 0)
            if p:targetPoint().y == 74 then
                local here = p:position()
                local found = p:nearbyUnits(here, 12, {houses={"Enemies"}, types={"HTK"}})
                assert(#found == 1 and found[1]:type() == "HTK")
                assert(p:firer() and p:firer():type() == "AMCV")
                assert(p:groundPoint(here).z < here.z)
                assert(p:action("PlayAnim", {Anim="PIFFPIFF", Target=here}))
                assert(p:action("PlaySound", {Sound="Explosion1", Target=here}))
                assert(p:action("SpawnParticleSystem", {ParticleSystem="SmallGreySSys", Target=here}))
                assert(p:action("FireWeapon", {Weapon="SFTestChildGun", Target=found[1]}))
            end
        elseif p:target() then
            p.state.unit = p:target()
            p:setVelocity(0, 0, 0.125)
        else
            p:setVelocity(4, 0, 0)
        end
        return 8
    end,
    onTick = function(p)
        assert(p.state.speed == p:weaponSpeed())
        if p.state.speed == 24 then
            assert(p:speed() == 0)
            if p:targetPoint().y == 61 then
                if p.age == 8 then
                    local pos = p:position()
                    p:moveTo(pos.x + 2, pos.y, pos.z)
                else
                    p:detonate()
                end
            end
        elseif p.state.unit then
            if p.age == 8 then
                assert(p.state.unit:alive() and p.state.unit == p:target())
                assert(p:velocity().z == 0.125 and p:heightAboveGround() > 0)
            end
            p:steerTo(p:targetPoint(), 0.5, 180)
        end
    end,
}
