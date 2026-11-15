local Spectrum = {}
function Spectrum.heights(bands,active)
    local result={}
    for i=1,64 do
        -- Bass meets at the center; high frequencies mirror toward the edges.
        local index=i<=32 and (33-i) or (i-32)
        local level=active and tonumber(bands[index]) or 0
        if not level or level~=level then level=0 end
        result[i]=math.max(2,math.min(1,math.max(0,level))*44)
    end
    return result
end
return Spectrum
