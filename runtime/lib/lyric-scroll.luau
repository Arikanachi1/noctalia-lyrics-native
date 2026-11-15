local Lyrics={}
local function content(data)
    data=data or {}
    local index=tonumber(data.index) or -1
    local lines=data.lines or {}
    local current=data.line or ''
    if current=='' then
        current=data.title or ''
        if (data.artist or '')~='' then current=current..' · '..data.artist end
    end
    local following=lines[index+2]
    local nextText=following and (following.text~='' and following.text or '♪') or ''
    local texts={}
    for i,line in ipairs(lines)do texts[i]=line.text or '' end
    local identity=(data.player or '')..'\n'..(data.trackId or ((data.title or '')..'\n'..(data.artist or '')))
        ..'\n'..tostring(data.lyricsId or 0)
    return {identity=identity,index=index,current=current,nextText=nextText,texts=texts}
end
function Lyrics.new(initial)
    local value=content(initial)
    return {from=value,target=value,position=value.index,velocity=0,elapsed=700,active=false}
end
function Lyrics.push(m,data)
    local value=content(data)
    local previous=m.target
    if value.identity==previous.identity and value.index==previous.index and value.current==previous.current then
        m.target=value
        if not m.active then m.from=value end
        return
    end
    local forward=value.identity==previous.identity and value.index==previous.index+1
        and previous.nextText~='' and previous.nextText==value.current
    if forward then
        if not m.active then m.from=previous end
        -- Retarget the same moving position and velocity. Replacing the rows
        -- mid-transition would jump the previous preview up by a whole slot.
        m.elapsed=0;m.active=true
        m.target=value
    else
        -- A seek, new track or replacement lyrics settle on the real position
        -- immediately rather than scrolling through unrelated stale lines.
        m.from=value;m.target=value;m.position=value.index;m.velocity=0;m.elapsed=700;m.active=false
    end
end
function Lyrics.tick(m,dt)
    if not m.active then return end
    dt=math.max(0,math.min(dt,1000))
    m.elapsed=m.elapsed+dt
    local seconds=dt/1000
    local delta=m.position-m.target.index
    local coefficient=m.velocity+12*delta
    local damping=math.exp(-12*seconds)
    local nextPosition=m.target.index+(delta+coefficient*seconds)*damping
    m.velocity=(m.velocity-12*coefficient*seconds)*damping
    m.position=math.min(m.target.index,math.max(m.position,nextPosition))
    if m.elapsed>=700 then
        m.position=m.target.index;m.velocity=0;m.from=m.target;m.active=false
    end
end
local function row(index,text,opacity,current)
    return {key='lyric-'..index,text=text,height=44,opacity=opacity,current=current}
end
local function ease(value)
    local p=math.max(0,math.min(1,value))
    return p*p*p*(p*(6*p-15)+10)
end
function Lyrics.frame(m)
    local value=m.target
    local function textAt(index)
        if index==value.index then return value.current end
        local text=value.texts[index+1]
        if text~=nil then return text~='' and text or '♪' end
        if index==m.from.index then return m.from.current end
        return ''
    end
    if not m.active then
        return {row(value.index-1,textAt(value.index-1),.18,false),
            row(value.index,value.current,1,true),row(value.index+1,value.nextText,.42,false)},0
    end
    local index=math.floor(m.position)
    local p=m.position-index
    -- Keep glyph layout and row heights fixed. Move the entire strip inside a
    -- clipped viewport; fade at the edges before text crosses the clip boundary.
    return {
        row(index-1,textAt(index-1),.18*(1-ease(p/.65)),false),
        row(index,textAt(index),1-.82*ease(p),true),
        row(index+1,textAt(index+1),.42+.58*ease(p),true),
        row(index+2,textAt(index+2),.42*ease((p-.2)/.8),false),
    },44*p
end
return Lyrics
