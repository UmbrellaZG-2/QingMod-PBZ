-- Qing v1: suppress accessory placeholders, preserving the gameplay pose source.
-- Keep component visibility intact for gameplay attachments and effects.
local TAG = "[QingResidualGuard] "
local BASE = "/HuaShira/Characters/Hero/Hero_Hun/"
local MODEL = "SkeletalMesh " .. BASE .. "SkinParts/Hun_Hair_Band.Hun_Hair_Band"
local whitelist = {}
for _, part in ipairs({
    "SkinParts/Hun_BrowLashCard",
    "SkinParts/Hun_BrowLashCardMoQu", "SkinParts/Hun_Hair",
    "SkinParts/Hun_Head_New_MoQu", "SkinParts/Hun_Head_New",
    "SkinParts/Hun_Head", "SkinParts/Hun_Scarf_Climax",
    "SkinParts/Hun_Scarf", "Hats/Hun_Hat_00", "Hats/Hun_Hat_01"
}) do
    local name = part:match("([^/]+)$")
    whitelist["SkeletalMesh " .. BASE .. part .. "." .. name] = true
end

local error_count = 0
local reported = {}
local function log(s)
    print(TAG .. tostring(s) .. "\n")
end
local function full(o)
    if not o or not o:IsValid() then return nil end
    return o:GetFullName()
end
local function mesh_of(c)
    local ok, mesh = pcall(function() return c:GetSkeletalMeshAsset() end)
    if ok and mesh and mesh:IsValid() then return mesh end
    return c:GetPropertyValue("SkeletalMesh")
end
local function live_component(c)
    local name = full(c)
    if not name or name:find("Default__", 1, true)
        or name:find("_GEN_VARIABLE", 1, true)
        or name:find("Archetype", 1, true) then return false end
    -- Live level instances only; IsRegistered() is not a reflected UFunction.
    if not name:find(":PersistentLevel.", 1, true) then return false end
    local owner = c:GetOwner()
    local owner_name = full(owner)
    return owner_name and not owner_name:find("Default__", 1, true)
        and not owner_name:find("_GEN_VARIABLE", 1, true)
end
local function scan()
    local components = FindAllOf("SkeletalMeshComponent") or {}
    local owners = {}
    -- Limit writes to actors that currently own the full 23-material Qing mesh.
    for _, c in ipairs(components) do
        if live_component(c) and full(mesh_of(c)) == MODEL and c:GetNumMaterials() == 23 then
            owners[full(c:GetOwner())] = true
        end
    end
    for _, c in ipairs(components) do
        if live_component(c) and owners[full(c:GetOwner())] then
            local mesh_name = full(mesh_of(c))
            if whitelist[mesh_name] then
                -- 0 = AlwaysTickPoseAndRefreshBones in this UE 5.7 build.
                -- Hiding a source must not make the attached MMD/weapon pose stale.
                if c:GetPropertyValue("VisibilityBasedAnimTickOption") ~= 0 then
                    c:SetPropertyValue("VisibilityBasedAnimTickOption", 0)
                end
                -- Hun_Body_Simple / CharacterMesh0 is deliberately excluded:
                -- gameplay props depend on the source component's render state.
                if c:GetPropertyValue("bRenderInMainPass") then c:SetRenderInMainPass(false) end
                if c:GetPropertyValue("bRenderInDepthPass") then c:SetRenderInDepthPass(false) end
                if c:GetPropertyValue("bVisibleInRayTracing") then c:SetVisibleInRayTracing(false) end
                if c:GetPropertyValue("CastShadow") then c:SetCastShadow(false) end
                local name = full(c)
                if not reported[name] then
                    reported[name] = true
                    log("placeholder_render_disabled component=" .. name .. " mesh=" .. mesh_name
                        .. " component_visibility_preserved=1 pose_tick=0")
                end
            end
        end
    end
end

log("loaded; version=Qing-1.0; whitelist=10; preserve_pose_source=1; preserve_component_visibility=1; body_source_untouched=1")
local pending = false
LoopAsync(500, function()
    -- Queue at most one scan if the game thread is blocked during loading.
    if pending then return end
    pending = true
    local ok, err = pcall(function()
        ExecuteInGameThread(function()
            local success, reason = pcall(scan)
            pending = false
            if not success and error_count < 10 then
                error_count = error_count + 1
                log("scan_error " .. tostring(reason))
            end
        end)
    end)
    if not ok then
        pending = false
        if error_count < 10 then
            error_count = error_count + 1
            log("queue_error " .. tostring(err))
        end
    end
end)
