local Motion=dofile("lib/motion.luau")
local first={player="test",title="Song",line="first",index=0,state="synced",playing=true}
local second={player="test",title="Song",line="second",index=1,state="synced",playing=true}
local motion=Motion.new(first)
Motion.push(motion,second)
local previousAlpha=1
for i=1,9 do
    Motion.tick(motion,16)
    local shown,alpha,offset=Motion.frame(motion)
    assert(shown.line=="first" and alpha<=previousAlpha and offset==0,"The outgoing line must fade without jumping")
    previousAlpha=alpha
end
Motion.tick(motion,16)
local shown,alpha,offset=Motion.frame(motion)
assert(shown.line=="second" and alpha<0.001 and offset==0,"Switch text only while invisible")
for _=1,20 do
    Motion.tick(motion,16)
    local _,nextAlpha,nextOffset=Motion.frame(motion)
    assert(nextAlpha>=alpha and nextOffset==0,"Smooth incoming line with a stable baseline")
    alpha=nextAlpha
end
assert(not motion.active and alpha==1)
Motion.push(motion,second)
assert(not motion.active,"Position updates must not restart the text animation")
Motion.push(motion,first);Motion.tick(motion,1000)
assert(not motion.active,"A long frame must settle immediately instead of slowing the transition")
local burst=Motion.new(first)
Motion.push(burst,second)
for index=1,4 do
    Motion.tick(burst,80)
    Motion.push(burst,{player="test",title="Song",line="word-"..index,index=index,state="synced",playing=true})
    local display,_,baseline=Motion.frame(burst)
    assert(burst.elapsed==index*80 and baseline==0,"Word-level updates must not restart the fade")
    if index>=2 then assert(display.line=="word-"..index,"Show the latest rapid line instead of staying stuck")end
end
print("Lyrics motion: stable baseline, smooth fades, no animation from position-only updates")
local songs=Motion.new({title='A'})
Motion.push(songs,{title='B'},true);Motion.tick(songs,320)
local visible,visibleAlpha=Motion.frame(songs)
assert(visible.title=='B' and math.abs(visibleAlpha-.5)<.001)
Motion.push(songs,{title='C'},true)
local retargeted,retargetAlpha=Motion.frame(songs)
assert(retargeted.title=='B' and math.abs(retargetAlpha-visibleAlpha)<.001,
    'Rapid song selection preserves visible artwork/text and opacity until they fade out')
Motion.tick(songs,160)
local replaced,replacedAlpha=Motion.frame(songs)
assert(replaced.title=='C' and replacedAlpha<.001,'Swap a retargeted song only while invisible')
Motion.tick(songs,320)
assert(not songs.active and select(2,Motion.frame(songs))==1)
