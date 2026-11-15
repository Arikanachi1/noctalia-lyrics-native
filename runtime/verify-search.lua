-- Exercise the real declarative panel with fake HTTP/IPC results.
local data={title="Song", artist="Ari", duration=20, position=5, offset=0, timingAvailable=true,
    trackId=string.rep("a",64), musicEligible=true, playing=true}
local watcher, tree, decoded, closed=nil,nil,{},false
local calls={}
local originalRequire=require
require=function(path)
    if path=="./lib/control.luau"then return dofile("lib/control.luau")end
    return originalRequire(path)
end
noctalia={
    pluginDir=function()return "/test/plugin"end,
    state={get=function()return data end,watch=function(_,fn)watcher=fn end},
    json={decode=function()return decoded end},
    runAsync=function(argv,callback)table.insert(calls,{argv=argv,callback=callback});return true end,
}
ui=setmetatable({},{__index=function(_,kind)return function(props,children)return {kind=kind,props=props,children=children or {}}end end})
panel={render=function(value)tree=value end,close=function()closed=true;onClose()end}
local function find(predicate,node)
    node=node or tree
    if predicate(node)then return node end
    for _,child in ipairs(node.children)do local result=find(predicate,child);if result then return result end end
end
local function button(text)return assert(find(function(node)return node.kind=="button" and node.props.text==text end))end
local function publish(nextData)data=nextData;watcher(data)end
local entry={id=7,title="Song",artist="Ari",duration=20,synced=true,plain="",matchesCurrent=true,
    lines={{time=0,text="First fixture line"},{time=3,text="Current fixture line"},{time=8,text="Next fixture line"}}}
dofile("search.luau");onOpen()
assert(calls[1].argv[2]=="--search" and calls[1].argv[4]=="--title")
decoded={results={entry}};calls[1].callback({exitCode=0,stdout="response"})
assert(not find(function(node)return node.kind=="button" and node.props.text=="Genius"end),"Search must stay inside the plugin")
find(function(node)return node.props.key=="record-7"end).props.onClick()
assert(find(function(node)return node.kind=="label" and node.props.text=="Current fixture line" and node.props.color=="primary"end),"Preview must start at the current playback position")
local nextData={};for key,value in pairs(data)do nextData[key]=value end;nextData.position=9;publish(nextData)
assert(find(function(node)return node.kind=="label" and node.props.text=="Next fixture line" and node.props.color=="primary"end),"Preview must follow the playback clock")
button("Показывать этот текст").props.onClick()
assert(calls[2].argv[2]=="--choose-lyrics" and calls[2].argv[3]==data.trackId)
decoded={ok=true};calls[2].callback({exitCode=0,stdout="response"});assert(closed)
closed=false;onOpen()
local pending=calls[#calls]
nextData={};for key,value in pairs(data)do nextData[key]=value end;nextData.trackId=string.rep("b",64);nextData.title="Different song";publish(nextData)
decoded={results={entry}};pending.callback({exitCode=0,stdout="response"})
assert(not find(function(node)return node.props.key=="record-7"end),"An old search must not attach results to a new song")
for _,call in ipairs(calls)do assert(call.argv[1]~="xdg-open" and call.argv[1]~="playerctl")end
print("Lyrics search: internal text, current position, selection and song-change guards passed")
