local data={musicEligible=true,playing=true,title='Song',artist='Artist',position=1,duration=20,index=0,
    state='synced',line='First',lines={{text='First',time=0},{text='Second',time=3}}}
local renders,watcher,tree=0,nil,nil
local originalRequire=require
require=function(path)
    if path=='./lib/motion.luau' then return dofile('lib/motion.luau')end
    if path=='./lib/control.luau' then return dofile('lib/control.luau')end
    return originalRequire(path)
end
noctalia={state={get=function()return data end,watch=function(_,fn)watcher=fn end},
    setUpdateInterval=function()end,runAsync=function()end,togglePanel=function()end}
ui=setmetatable({},{__index=function(_,kind)return function(props,children)return {kind=kind,props=props,children=children or {}}end end})
panel={render=function(value)renders=renders+1;tree=value end,setNeedsFrameTick=function()end}
dofile('panel.luau');onOpen();local before=renders
data.source='LRCLIB';data.message='Синхронизированный текст';data.manualMatch=true;watcher(data)
assert(renders==before,'Hidden source/status notes must not cause lyric layout updates')
data.position=1.16;watcher(data);update()
assert(renders==before,'Long lyrics tree must not rebuild for subsecond position-only updates')
data.position=2.1;watcher(data)
assert(renders==before+1,'Progress still updates once per elapsed second')
local function find(key,node)
    node=node or tree;if node.props.key==key then return node end
    for _,child in ipairs(node.children)do local result=find(key,child);if result then return result end end
end
assert(tree.props.width==640 and tree.props.height==540,'Lyrics must reserve a bounded area for scrolling and footer')
assert(find('lyrics-footer').kind=='column','Footer actions must use separate bounded rows')
local transport=find('lyrics-transport')
local transportWidth=(#transport.children-1)*transport.props.gap
for _,button in ipairs(transport.children)do transportWidth=transportWidth+assert(button.props.width)end
assert(transportWidth<=608,'Transport must fit inside panel padding')
data.player='org.mpris.MediaPlayer2.firefox.test';data.timingAvailable=false;watcher(data)
assert(find('restore-row').props.visible==true and find('restore-clock'), 'Missing Firefox timing keeps recovery available')
for _,button in ipairs(find('lyrics-transport').children)do
    assert(button.props.key~='restore-clock','Optional recovery must not overflow the transport row')
end
data.timingAvailable=true;data.trackId='song';watcher(data)
assert(find('offset-row').props.visible==true and find('restore-row').props.visible==false,
    'Offset and recovery actions occupy their own footer rows')
data.lines={}
for i=1,100 do data.lines[i]={text=string.rep('Long lyric ',20)}end
watcher(data);toggleAll()
assert(find('lyrics-footer').props.height==80 and find('lyrics-scroll').props.flexGrow==1,
    'Long complete lyrics consume the scroll area while reserving the footer')
assert(find('lyrics-mode').props.text=='Следить за песней','Complete lyrics keep the follow action accessible')
onClose();before=renders;onFrameTick(16)
assert(renders==before,'Closed lyrics panel ignores late frames')
print('Full lyrics: cached text layout, second-level progress and close lifecycle passed')
