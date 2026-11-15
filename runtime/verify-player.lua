local data={musicEligible=true,playing=true,title='Song',artist='Artist',line='Words',index=0,trackId='a',
    lines={{text='Words'},{text='Next words'},{text='Later words'}},player='org.mpris.MediaPlayer2.test'}
local now,rendered,stream,lease,tree=1000,0,nil,nil,nil
local watchers={}
local toggled=nil
local oldRequire=require
require=function(path)
    if path=='./lib/motion.luau' then return dofile('lib/motion.luau') end
    if path=='./lib/lyric-scroll.luau' then return dofile('lib/lyric-scroll.luau') end
    if path=='./lib/spectrum-view.luau' then return dofile('lib/spectrum-view.luau') end
    return oldRequire(path)
end
noctalia={
    nowMs=function()return now end,pluginDir=function()return '/test/plugin' end,
    getenv=function()return '/test/runtime' end,mkdirAll=function()return true end,
    writeFile=function(_,value)lease=tonumber(value);return true end,
    state={get=function()return data end,watch=function(k,fn)watchers[k]=fn end},
    setUpdateInterval=function()end,runAsync=function()return true end,
    runStream=function(_,fn)
        assert(not data.playing or (lease and lease>now),'Capture lease must exist before companion startup')
        stream=fn;return true
    end,
    json={decode=function()return {available=true,bands={.3,.8,.2,.1,.5,.6,.7,.9,.2,.1,.4,.8,.5,.3,.2,.1,
        .1,.7,.2,.5,.4,.2,.3,.6,.8,.2,.4,.3,.8,.1,.5,.9}} end},
    togglePanel=function(id)toggled=id end,
}
ui=setmetatable({},{__index=function(_,kind)return function(props,children)return {kind=kind,props=props,children=children or {}}end end})
panel={render=function(value)tree=value;rendered=rendered+1 end,setNeedsFrameTick=function()end}
local function find(key,node)
    node=node or tree;if node.props.key==key then return node end
    for _,child in ipairs(node.children)do local found=find(key,child);if found then return found end end
end
dofile('player.luau')
assert(rendered==0 and lease==nil,'Closed panel must not acquire audio')
onOpen();assert(lease==3500 and stream~=nil,'Opening during playback grants a short capture lease')
local beforeFrame=rendered;stream('frame')
assert(rendered==beforeFrame,'Spectrum callback schedules presentation without rebuilding all text immediately')
now=1017;onFrameTick(17)
assert(rendered>beforeFrame,'Fresh spectrum must paint on the next display frame without another 32ms wait')
local spectrum=assert(find('spectrum'))
local timeline=assert(find('timeline'))
assert(timeline.children[1].props.key=='progress' and timeline.children[2]==spectrum and (timeline.props.gap or 0)==0,
    'Spectrum must attach directly below the track progress')
assert(spectrum.props.align=='start' and #spectrum.children==64,'64 bars grow down from the progress line')
local totalWidth=(#spectrum.children-1)*spectrum.props.gap
for _,bar in ipairs(spectrum.children)do totalWidth=totalWidth+bar.props.width end
assert(totalWidth<=384,'All 64 bars must fit below the progress without clipping')
for i=1,32 do
    assert(spectrum.children[i].props.height==spectrum.children[65-i].props.height,'Bars mirror around the center')
end
find('open-lyrics').props.onClick()
assert(toggled=='ari/lyrics:panel','Full lyrics action must remain directly accessible')
find('open-search').props.onClick()
assert(toggled=='ari/lyrics:search','Search action must remain directly accessible')
local controls=assert(find('controls'))
local actions={};for _,child in ipairs(controls.children)do actions[child.props.key]=true end
assert(actions['open-lyrics'] and actions['open-search'] and actions.play,'Text and search belong beside playback controls')
assert(find('lyric-preview').kind=='scroll','Lyrics need a clipped viewport instead of shrinking text')
assert(find('lyric-strip').children[3].children[1].props.text=='Next words','Freed space displays the next actual lyric')
data.index=1;data.line='Next words';watchers.track(data)
now=1034;onFrameTick(17)
assert(#find('lyric-strip').children==4,'Sequential lyrics use a scrolling transition')
for _,row in ipairs(find('lyric-strip').children)do assert(row.props.height==44,'Native text row height stays constant during movement')end
now=1734;onFrameTick(700)
assert(#find('lyric-strip').children==3 and find('lyric-strip').children[2].children[1].props.text=='Next words',
    'Final scroll frame must paint even when the animation just became inactive')
data={musicEligible=true,playing=true,title='Other song',artist='Other artist',line='New words',index=0,trackId='b',
    lines={{text='New words'},{text='New preview'}},player='org.mpris.MediaPlayer2.test'}
watchers.track(data)
now=1814;onFrameTick(80)
assert(find('title').props.text=='Song' and find('song-header').props.opacity>0 and find('song-header').props.opacity<1,
    'Outgoing song must fade without replacing its visible metadata')
now=1894;onFrameTick(80)
assert(find('title').props.text=='Other song' and find('song-header').props.opacity<.001,
    'Replace song and lyric only while invisible')
now=2214;onFrameTick(320)
assert(find('song-header').props.opacity==1 and find('title').props.text=='Other song','Next song fades in completely')
data.lines={};data.line='';data.index=-1;watchers.track(data)
now=2694;onFrameTick(480)
data.lines={{text='Loaded words'},{text='Loaded preview'}};data.line='Loaded words';data.index=0;watchers.track(data)
now=2774;onFrameTick(80)
assert(find('song-header').props.opacity<1,'Lyrics arriving after loading must also fade instead of snapping in')
now=3174;onFrameTick(400)
data={musicEligible=true,playing=true,title='Third song',artist='Artist',line='Third words',index=0,trackId='c',
    lines={{text='Third words'}},player='org.mpris.MediaPlayer2.test'}
watchers.track(data)
now=3494;onFrameTick(320)
assert(find('title').props.text=='Third song' and math.abs(find('song-header').props.opacity-.5)<.001)
data={musicEligible=true,playing=true,title='Fourth song',artist='Artist',line='Fourth words',index=0,trackId='d',
    lines={{text='Fourth words'}},player='org.mpris.MediaPlayer2.test'}
watchers.track(data)
now=3510;onFrameTick(16)
assert(find('title').props.text=='Third song','Rapid song changes must preserve the visible song until it fades out')
now=3654;onFrameTick(144)
assert(find('title').props.text=='Fourth song' and find('song-header').props.opacity<.001)
now=3974;onFrameTick(320)
assert(find('song-header').props.opacity==1)
local count=rendered
onClose();assert(lease==0,'Closing revokes capture immediately')
stream('late frame');assert(rendered==count,'Late spectrum callback cannot render closed panel')
onOpen();data.playing=false;watchers.track(data)
assert(lease==0,'Pausing must stop capture even with panel open')
now=5000;update();assert(lease==0,'Periodic refresh must not renew paused capture')
data.playing=true;watchers.track(data);assert(lease==7500,'Resume renews capture')
onClose()
print('Player: open/close, stale frames, pause and resume capture lifecycle passed')
