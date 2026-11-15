local data={musicEligible=true,playing=true,title='Song',artist='Artist',line='Words',trackId='a',position=1}
local now,tree,watcher,renders=1000,nil,nil,0
local time='12:34'
local toggled,command=nil,nil
local originalRequire=require
require=function(path)
    if path=='./lib/island.luau' then return dofile('lib/island.luau') end
    return originalRequire(path)
end
noctalia={nowMs=function()return now end,
    state={get=function()return data end,watch=function(_,fn)watcher=fn end},
    formatTime=function(format)assert(format=='{:%H:%M}','Split clock must request hours/minutes without seconds or AM/PM');return time end,
    timeFormat=function()return '{:%I:%M:%S %p}' end,
    setUpdateInterval=function()end,runAsync=function(argv)command=argv end,togglePanel=function(id)toggled=id end}
ui=setmetatable({},{__index=function(_,kind)return function(props,children)return {kind=kind,props=props,children=children or {}}end end})
barWidget={render=function(value)tree=value;renders=renders+1 end,clearTooltip=function()end}
local function find(key,node)
    node=node or tree;if node.props.key==key then return node end
    for _,child in ipairs(node.children)do local found=find(key,child);if found then return found end end
end
dofile('widget.luau')
assert(find('clock-full') and find('clock-full').props.text=='12:34',
    'Collapsed clock must be one label, without padding between its digits')
now=2000;update()
assert(find('clock-full').props.text=='12:34','Time remains one readable string during opening')
assert((find('clock-pill').props.opacity or 1)>=.8,'Clock stays readable independently of music fades')
now=3000;update();now=3200;update()
assert(find('island-content').props.text=='Words','Music appears beside the complete clock')
assert(find('music-slot').kind=='column' and find('music-slot').props.flexGrow==1,
    'Native Flex must allocate the remaining horizontal space directly to music')
assert(not find('music-viewport'),'Compact music must not depend on nested ScrollView measurement')
assert(find('island-content').props.maxWidth>=270,'Expanded lyrics need at least270px of readable text space')
assert(not find('clock-hours') and not find('clock-minutes'),'Time must never split into separate glyph groups')
assert(tree.children[#tree.children].props.key=='clock-pill','Clock occupies the trailing position')
onHover(true)
assert(find('clock-full').props.text=='12:34','Hover keeps time visible')
onHover(false)
assert(tree.props.fill==nil and tree.props.border==nil,'Native capsule owns both background and hover geometry')
local before=renders
data.position=1.16;now=3360;watcher(data)
assert(renders==before,'Position-only updates must not rebuild the compact label')
now=4000;update()
assert(renders==before,'Stable music label must not redraw on idle ticks')
time='12:35';now=4100;update()
assert(find('clock-full').props.text=='12:35','Time updates even while music remains unchanged')
onClick()
assert(toggled=='ari/lyrics:player','Music click opens the player')
data={};watcher(data);now=5100;update();now=6100;update();now=6300;update();onClick()
assert(find('clock-full') and find('clock-full').props.text=='12:35','Closing restores one contiguous clock label')
assert(command and command[3]=='panel-toggle' and command[4]=='control-center','Clock click opens the Noctalia menu')
print('Widget: one native capsule and no position-only text redraws')
