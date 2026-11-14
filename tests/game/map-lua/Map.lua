return {
 native=function(ctx)
  assert(ctx:house().index == ctx.owner)
  assert(ctx:action("GiveResearchPoints",{Amount=7,Target="Owner"}))
  assert(ctx:schedule(40,"mapLater",5))
 end,
 mapLater=function(ctx,n)
  assert(ctx:action("GiveResearchPoints",{Amount=n,Target="Owner"}))
 end,
 object=function(ctx)
  assert(ctx.self and ctx.self:alive() and ctx.point and ctx.owner==ctx:house().index)
  assert(ctx:schedule(30,"objectLater",ctx.self:id()))
 end,
 objectLater=function(ctx,id)
  assert(ctx.self and ctx.self:alive() and ctx.self:id()==id and ctx.point and ctx:house().index==ctx.owner)
  assert(ctx:action("GiveResearchPoints",{Amount=6,Target="Owner"}))
 end,
 start=function(ctx)
  assert(ctx.self == nil and ctx.point == nil and ctx.target == nil)
  assert(ctx.owner == ctx:house().index and ctx.params.amount==123)
  assert(ctx:schedule(30,"later",ctx.params.amount))
 end,
 later=function(ctx,n)
  assert(ctx.self == nil and ctx.point == nil and ctx:house().index == ctx.owner)
  assert(ctx:action("GiveCredits",{Amount=n,Target="Owner"}))
 end,
}
