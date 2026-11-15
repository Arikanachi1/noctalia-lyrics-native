local Lyrics=dofile('lib/lyric-scroll.luau')
local lines={{text='First'},{text='Second'},{text='Third'},{text='Fourth'}}
local function track(index,id)
    return {player='test',trackId=id or 'song',title='Song',artist='Artist',index=index,
        state='synced',line=lines[index+1] and lines[index+1].text or '',lines=lines}
end
local model=Lyrics.new(track(0))
local rows=Lyrics.frame(model)
assert(#rows==3 and rows[1].text=='' and rows[2].text=='First' and rows[3].text=='Second')
assert(rows[3].opacity<rows[2].opacity,'Upcoming lyric must be subdued')
Lyrics.push(model,track(0));assert(not model.active,'Position updates do not restart the scroll')
Lyrics.push(model,track(1));assert(model.active)
local previousTop=88
for _=1,3 do
    Lyrics.tick(model,175);local offset;rows,offset=Lyrics.frame(model)
    assert(#rows==4 and rows[3].text=='Second' and rows[4].text=='Third')
    assert(type(offset)=='number' and 88-offset<previousTop,'Upcoming lyric moves monotonically into the current slot')
    for _,row in ipairs(rows)do assert(row.height==44,'Scrolling must never shrink the text block')end
    assert(rows[3].opacity>rows[4].opacity,'Promoted lyric stays brighter than its preview')
    previousTop=88-offset
end
Lyrics.tick(model,175);rows=Lyrics.frame(model)
assert(not model.active and #rows==3 and rows[1].text=='First' and rows[2].text=='Second' and rows[3].text=='Third')
Lyrics.push(model,track(0));rows=Lyrics.frame(model)
assert(not model.active and rows[2].text=='First','Backward seek settles on the actual line')
Lyrics.push(model,track(1));Lyrics.tick(model,100)
local before={};local firstRows,firstOffset=Lyrics.frame(model);local top=-(firstOffset or 0)
for _,row in ipairs(firstRows)do before[row.key]={top=top,opacity=row.opacity};top=top+row.height end
Lyrics.push(model,track(2));local nextRows,nextOffset=Lyrics.frame(model);top=-(nextOffset or 0)
for _,row in ipairs(nextRows)do
    local previous=before[row.key]
    if previous then
        assert(math.abs(previous.top-top)<.001 and math.abs(previous.opacity-row.opacity)<.001,
            'Rapid lyric changes preserve the displayed position and opacity of each line')
    end
    top=top+row.height
end
Lyrics.tick(model,700);rows=Lyrics.frame(model)
assert(not model.active and rows[2].text=='Third' and rows[3].text=='Fourth')
Lyrics.push(model,{player='test',trackId='other',title='Other',artist='Band',index=-1,line='',lines={}})
rows=Lyrics.frame(model)
assert(not model.active and rows[2].text=='Other · Band' and rows[3].text=='','New song clears old preview')
print('Lyric scroll: subdued next line, fixed viewport, promotion, seeking and rapid timestamps passed')
