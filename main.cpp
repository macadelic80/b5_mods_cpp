// Example WebAssembly mod for Burnout_PC.exe (b5-decomp) in C++ with Dear ImGui -- the C/C++
// counterpart of the Rust + egui debug menu (github.com/macadelic80/b5_mods_rs).
//
// F8 opens / closes a window with the player's position and car, a teleport target, and the
// game's own debug menu tree (brn.debugvars.v1). ImGui's window layout is kept in the mod's
// storage (mods/imgui_example/data/imgui.ini). Build: see build.py next to this file.
//
// The whole renderer is ~60 lines: ImGui's ImDrawVert is brn_vertex byte for byte, its 16-bit
// indices go to brn_ui_submit16, and its 1.92 texture requests (ImTextureData) map onto
// brn_ui_texture_create / update / destroy.

#include "brn_mod_api.h"   // copy of b5-decomp src/pc/mods/api/brn_mod_api.h
#include "imgui.h"

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static_assert(sizeof(ImDrawVert) == sizeof(brn_vertex), "ImDrawVert must be pos, uv, col (no IMGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT)");
static_assert(sizeof(ImDrawIdx) == 2, "brn_ui_submit16 takes 16-bit indices");

namespace
{
    brn_input_event gaInput[BRN_INPUT_BUFFER_CAPACITY];
    bool            gbOpen = false;
    float           gafTarget[3] = {};
    char            gacSearch[64] = {};
    double          gfLastIniSave = 0.0;
    const char*     KPC_INI_FILE = "imgui.ini";

    // ---- ImGui 1.92 texture requests -> brn.ui.v1 -----------------------------------------------

    void UpdateTexture(ImTextureData* lpTexture)
    {
        if (lpTexture->Status == ImTextureStatus_WantCreate)
        {
            IM_ASSERT(lpTexture->Format == ImTextureFormat_RGBA32);
            const uint32_t luHandle = brn_ui_texture_create(static_cast<const uint8_t*>(lpTexture->GetPixels()),
                                                            lpTexture->Width, lpTexture->Height);
            lpTexture->SetTexID(static_cast<ImTextureID>(luHandle));
            lpTexture->SetStatus(ImTextureStatus_OK);
        }
        else if (lpTexture->Status == ImTextureStatus_WantUpdates)
        {
            // The host takes a tightly packed rectangle; ImGui's rows are a full texture width apart.
            const ImTextureRect& lrRect = lpTexture->UpdateRect;
            std::vector<uint8_t> laRows(static_cast<size_t>(lrRect.w) * lrRect.h * 4u);
            for (int liRow = 0; liRow < lrRect.h; ++liRow)
                std::memcpy(&laRows[static_cast<size_t>(liRow) * lrRect.w * 4u],
                            lpTexture->GetPixelsAt(lrRect.x, lrRect.y + liRow), static_cast<size_t>(lrRect.w) * 4u);
            brn_ui_texture_update(static_cast<uint32_t>(lpTexture->TexID), lrRect.x, lrRect.y, lrRect.w, lrRect.h, laRows.data());
            lpTexture->SetStatus(ImTextureStatus_OK);
        }
        else if (lpTexture->Status == ImTextureStatus_WantDestroy && lpTexture->UnusedFrames > 0)
        {
            brn_ui_texture_destroy(static_cast<uint32_t>(lpTexture->TexID));
            lpTexture->SetTexID(ImTextureID_Invalid);
            lpTexture->SetStatus(ImTextureStatus_Destroyed);
        }
    }

    void RenderDrawData(const ImDrawData* lpData)
    {
        if (lpData->Textures != nullptr)
            for (ImTextureData* lpTexture : *lpData->Textures)
                if (lpTexture->Status != ImTextureStatus_OK) UpdateTexture(lpTexture);
        const ImVec2 lOrigin = lpData->DisplayPos;
        const ImVec2 lScale = lpData->FramebufferScale;
        for (const ImDrawList* lpList : lpData->CmdLists)
            for (const ImDrawCmd& lrCommand : lpList->CmdBuffer)
            {
                if (lrCommand.UserCallback != nullptr || lrCommand.ElemCount == 0) continue;
                const brn_rect lClip = { (lrCommand.ClipRect.x - lOrigin.x) * lScale.x, (lrCommand.ClipRect.y - lOrigin.y) * lScale.y,
                                         (lrCommand.ClipRect.z - lOrigin.x) * lScale.x, (lrCommand.ClipRect.w - lOrigin.y) * lScale.y };
                // Indices are relative to VtxOffset: hand the host the vertices from there on.
                const ImDrawVert* lpVertices = lpList->VtxBuffer.Data + lrCommand.VtxOffset;
                const uint32_t luVertices = static_cast<uint32_t>(lpList->VtxBuffer.Size) - lrCommand.VtxOffset;
                brn_ui_submit(reinterpret_cast<const brn_vertex*>(lpVertices), luVertices,
                              lpList->IdxBuffer.Data + lrCommand.IdxOffset, lrCommand.ElemCount,
                              static_cast<uint32_t>(lrCommand.GetTexID()), lClip, 0 /* straight alpha */);
            }
    }

    // ---- input -------------------------------------------------------------------------------------

    ImGuiKey ImGuiKeyFor(int32_t liCode)
    {
        if (liCode >= 'A' && liCode <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + (liCode - 'A'));
        if (liCode >= '0' && liCode <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + (liCode - '0'));
        if (liCode >= BRN_KEY_F1 && liCode <= BRN_KEY_F12) return static_cast<ImGuiKey>(ImGuiKey_F1 + (liCode - BRN_KEY_F1));
        if (liCode >= BRN_KEY_KP_0 && liCode <= BRN_KEY_KP_9) return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (liCode - BRN_KEY_KP_0));
        switch (liCode)
        {
        case BRN_KEY_SPACE:       return ImGuiKey_Space;
        case BRN_KEY_MINUS:       return ImGuiKey_Minus;
        case BRN_KEY_PERIOD:      return ImGuiKey_Period;
        case BRN_KEY_COMMA:       return ImGuiKey_Comma;
        case BRN_KEY_SLASH:       return ImGuiKey_Slash;
        case BRN_KEY_ESCAPE:      return ImGuiKey_Escape;
        case BRN_KEY_ENTER:       return ImGuiKey_Enter;
        case BRN_KEY_KP_ENTER:    return ImGuiKey_KeypadEnter;
        case BRN_KEY_TAB:         return ImGuiKey_Tab;
        case BRN_KEY_BACKSPACE:   return ImGuiKey_Backspace;
        case BRN_KEY_INSERT:      return ImGuiKey_Insert;
        case BRN_KEY_DELETE:      return ImGuiKey_Delete;
        case BRN_KEY_LEFT:        return ImGuiKey_LeftArrow;
        case BRN_KEY_RIGHT:       return ImGuiKey_RightArrow;
        case BRN_KEY_UP:          return ImGuiKey_UpArrow;
        case BRN_KEY_DOWN:        return ImGuiKey_DownArrow;
        case BRN_KEY_PAGE_UP:     return ImGuiKey_PageUp;
        case BRN_KEY_PAGE_DOWN:   return ImGuiKey_PageDown;
        case BRN_KEY_HOME:        return ImGuiKey_Home;
        case BRN_KEY_END:         return ImGuiKey_End;
        case BRN_KEY_LEFT_SHIFT:  return ImGuiKey_LeftShift;
        case BRN_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
        case BRN_KEY_LEFT_CTRL:   return ImGuiKey_LeftCtrl;
        case BRN_KEY_RIGHT_CTRL:  return ImGuiKey_RightCtrl;
        case BRN_KEY_LEFT_ALT:    return ImGuiKey_LeftAlt;
        case BRN_KEY_RIGHT_ALT:   return ImGuiKey_RightAlt;
        default:                  return ImGuiKey_None;
        }
    }

    void SetOpen(bool lbOpen)
    {
        gbOpen = lbOpen;
        ImGui::GetIO().MouseDrawCursor = lbOpen;   // the game hides the Windows cursor
        brn_set_input_capture(lbOpen ? (BRN_CAPTURE_KEYBOARD | BRN_CAPTURE_MOUSE) : 0u);
    }

    void HandleInput(const brn_input_event& lrEvent)
    {
        ImGuiIO& lrIo = ImGui::GetIO();
        lrIo.AddKeyEvent(ImGuiMod_Ctrl, (lrEvent.modifiers & BRN_MODIFIER_CTRL) != 0);
        lrIo.AddKeyEvent(ImGuiMod_Shift, (lrEvent.modifiers & BRN_MODIFIER_SHIFT) != 0);
        lrIo.AddKeyEvent(ImGuiMod_Alt, (lrEvent.modifiers & BRN_MODIFIER_ALT) != 0);
        switch (lrEvent.type)
        {
        case BRN_INPUT_MOUSE_MOVE:   lrIo.AddMousePosEvent(lrEvent.x, lrEvent.y); break;
        case BRN_INPUT_MOUSE_BUTTON:
            lrIo.AddMousePosEvent(lrEvent.x, lrEvent.y);
            if (lrEvent.code <= BRN_MOUSE_MIDDLE) lrIo.AddMouseButtonEvent(lrEvent.code, lrEvent.pressed != 0);
            break;
        case BRN_INPUT_MOUSE_WHEEL:  lrIo.AddMouseWheelEvent(lrEvent.x, lrEvent.y); break;
        case BRN_INPUT_MOUSE_LEAVE:  lrIo.AddMousePosEvent(-FLT_MAX, -FLT_MAX); break;
        case BRN_INPUT_TEXT:         lrIo.AddInputCharacter(static_cast<unsigned int>(lrEvent.code)); break;
        case BRN_INPUT_FOCUS:        lrIo.AddFocusEvent(lrEvent.pressed != 0); break;
        case BRN_INPUT_KEY:
            if (lrEvent.code == BRN_KEY_F8 && lrEvent.pressed && !lrEvent.repeat) { SetOpen(!gbOpen); break; }
            if (lrEvent.code == BRN_KEY_ESCAPE && lrEvent.pressed && gbOpen) { SetOpen(false); break; }
            if (const ImGuiKey leKey = ImGuiKeyFor(lrEvent.code)) lrIo.AddKeyEvent(leKey, lrEvent.pressed != 0);
            break;
        default: break;
        }
    }

    // ---- the UI ------------------------------------------------------------------------------------

    std::string Text(const char* lpcField, size_t luSize) { return std::string(lpcField, strnlen(lpcField, luSize)); }

    void PlayerPanel()
    {
        BrnPlayerState lState = {};
        if (!brn_player_get_state(&lState))
        {
            ImGui::TextDisabled("No player car yet.");
            return;
        }
        ImGui::Text("Car: %s (%s)", Text(lState.car_name, sizeof(lState.car_name)).c_str(),
                    Text(lState.car_id_text, sizeof(lState.car_id_text)).c_str());
        ImGui::Text("Position  %.1f  %.1f  %.1f", lState.position.x, lState.position.y, lState.position.z);
        ImGui::Text("Speed %.0f mph, gear %d", lState.speed_mph, lState.gear);
        if (ImGui::Button("Use as target"))
        {
            gafTarget[0] = lState.position.x; gafTarget[1] = lState.position.y; gafTarget[2] = lState.position.z;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::DragFloat3("##target", gafTarget, 1.0f);
        ImGui::SameLine();
        if (ImGui::Button("Teleport")) brn_player_teleport(gafTarget[0], gafTarget[1], gafTarget[2], 0.0f, 0.0f, 0.0f);
    }

    struct DebugRow { BrnDebugVar mVar; int miIndex; };

    void DebugVarRow(const DebugRow& lrRow)
    {
        const BrnDebugVar& lrVar = lrRow.mVar;
        ImGui::PushID(lrVar.path);
        char lacValue[64];
        switch (lrVar.kind)
        {
        case BRN_DEBUGVAR_FUNCTION:
            if (ImGui::SmallButton(lrVar.name)) brn_debugvars_call(lrVar.path);
            break;
        case BRN_DEBUGVAR_SECTION:
            if (lrVar.value == 0.0 && ImGui::SmallButton("Open")) brn_debugvars_call(lrVar.path);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", lrVar.name);
            break;
        case BRN_DEBUGVAR_BOOL:
        {
            bool lbOn = lrVar.value != 0.0;
            if (ImGui::Checkbox(lrVar.name, &lbOn) && !lrVar.read_only) brn_debugvars_set(lrVar.path, lbOn ? "true" : "false");
            break;
        }
        case BRN_DEBUGVAR_INT: case BRN_DEBUGVAR_UINT: case BRN_DEBUGVAR_CHOICE:
        {
            int liValue = static_cast<int>(lrVar.value);
            if (ImGui::DragInt(lrVar.name, &liValue, 0.25f) && !lrVar.read_only)
            {
                std::snprintf(lacValue, sizeof(lacValue), "%d", liValue);
                brn_debugvars_set(lrVar.path, lacValue);
            }
            break;
        }
        case BRN_DEBUGVAR_FLOAT:
        {
            float lfValue = static_cast<float>(lrVar.value);
            if (ImGui::DragFloat(lrVar.name, &lfValue, 0.01f) && !lrVar.read_only)
            {
                std::snprintf(lacValue, sizeof(lacValue), "%g", lfValue);
                brn_debugvars_set(lrVar.path, lacValue);
            }
            break;
        }
        default:
            ImGui::Text("%s: %s", lrVar.name, lrVar.text);
            break;
        }
        ImGui::PopID();
    }

    // The flat, depth-first listing back into folders; liAt walks it.
    void DebugTree(const std::vector<DebugRow>& lrRows, size_t& lrAt, int liDepth)
    {
        while (lrAt < lrRows.size() && lrRows[lrAt].mVar.depth >= liDepth)
        {
            const DebugRow& lrRow = lrRows[lrAt++];
            if (lrRow.mVar.kind != BRN_DEBUGVAR_MENU) { DebugVarRow(lrRow); continue; }
            if (ImGui::TreeNode(lrRow.mVar.path, "%s", lrRow.mVar.name))
            {
                // Expanded: open the component sections inside, so their controls get listed.
                for (size_t lu = lrAt; lu < lrRows.size() && lrRows[lu].mVar.depth > liDepth; ++lu)
                    if (lrRows[lu].mVar.depth == liDepth + 1 && lrRows[lu].mVar.kind == BRN_DEBUGVAR_SECTION && lrRows[lu].mVar.value == 0.0)
                        brn_debugvars_call(lrRows[lu].mVar.path);
                DebugTree(lrRows, lrAt, liDepth + 1);
                ImGui::TreePop();
            }
            else
                while (lrAt < lrRows.size() && lrRows[lrAt].mVar.depth > liDepth) ++lrAt;   // skip the folder's contents
        }
    }

    void DebugVarsPanel()
    {
        static std::vector<DebugRow> saRows;
        static uint32_t suGeneration = 0;
        const uint32_t luGeneration = brn_debugvars_refresh();
        if (luGeneration == 0) { ImGui::TextDisabled("Waiting for the game's debug menu..."); return; }
        if (luGeneration != suGeneration)
        {
            suGeneration = luGeneration;
            saRows.clear();
            const int liCount = brn_debugvars_count();
            for (int li = 0; li < liCount; ++li)
            {
                DebugRow lRow = {};
                lRow.miIndex = li;
                if (brn_debugvars_get(li, &lRow.mVar)) saRows.push_back(lRow);
            }
        }
        ImGui::InputText("Search", gacSearch, sizeof(gacSearch));
        ImGui::BeginChild("tree", ImVec2(0, 360), ImGuiChildFlags_Borders);
        if (gacSearch[0] != '\0')
        {
            for (const DebugRow& lrRow : saRows)
                if (lrRow.mVar.kind != BRN_DEBUGVAR_MENU && std::strstr(lrRow.mVar.path, gacSearch) != nullptr) DebugVarRow(lrRow);
        }
        else
        {
            size_t luAt = 0;
            DebugTree(saRows, luAt, 0);
        }
        ImGui::EndChild();
    }

    void SaveIni()
    {
        size_t luSize = 0;
        const char* lpcIni = ImGui::SaveIniSettingsToMemory(&luSize);
        brn_storage_write(KPC_INI_FILE, lpcIni, static_cast<uint32_t>(luSize));
        ImGui::GetIO().WantSaveIniSettings = false;
    }
}

// ---- the exports ------------------------------------------------------------------------------------

extern "C"
{
    BRN_DEFINE_API_VERSION()
    BRN_DEFINE_INPUT_BUFFER(gaInput)

    BRN_EXPORT("brn_mod_init") int32_t brn_mod_init(void)
    {
        ImGui::CreateContext();
        ImGuiIO& lrIo = ImGui::GetIO();
        lrIo.BackendRendererName = "brn.ui.v1";
        lrIo.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
        lrIo.IniFilename = nullptr;   // no file system in the sandbox: the layout goes to brn.storage.v1
        // Copy works (the host writes the clipboard); paste does not: a mod cannot read it.
        ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* lpcText) { brn_ui_set_clipboard_text(lpcText); };
        std::vector<char> laIni(64 * 1024);
        const int32_t liSize = brn_storage_read(KPC_INI_FILE, laIni.data(), static_cast<uint32_t>(laIni.size()));
        if (liSize > 0 && static_cast<size_t>(liSize) <= laIni.size()) ImGui::LoadIniSettingsFromMemory(laIni.data(), static_cast<size_t>(liSize));
        brn_log("ImGui example ready -- F8 to open");
        return 1;
    }

    BRN_EXPORT("brn_mod_on_input") void brn_mod_on_input(uint32_t luCount)
    {
        for (uint32_t lu = 0; lu < luCount && lu < BRN_INPUT_BUFFER_CAPACITY; ++lu) HandleInput(gaInput[lu]);
    }

    BRN_EXPORT("brn_mod_render") void brn_mod_render(uint32_t luWidth, uint32_t luHeight, float lfDpiScale,
                                                     float lfDeltaSeconds, uint64_t)
    {
        ImGuiIO& lrIo = ImGui::GetIO();
        lrIo.DisplaySize = ImVec2(static_cast<float>(luWidth), static_cast<float>(luHeight));
        lrIo.DeltaTime = lfDeltaSeconds > 0.0f ? lfDeltaSeconds : 1.0f / 60.0f;
        lrIo.FontGlobalScale = lfDpiScale;
        ImGui::NewFrame();
        if (gbOpen)
        {
            ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(520, 560), ImGuiCond_FirstUseEver);
            bool lbOpen = true;
            if (ImGui::Begin("B5 ImGui example (C++)", &lbOpen))
            {
                PlayerPanel();
                ImGui::Separator();
                if (ImGui::CollapsingHeader("Game debug menu", ImGuiTreeNodeFlags_DefaultOpen)) DebugVarsPanel();
            }
            ImGui::End();
            if (!lbOpen) SetOpen(false);
        }
        ImGui::Render();
        RenderDrawData(ImGui::GetDrawData());
        if (lrIo.WantSaveIniSettings && brn_time() - gfLastIniSave > 2.0)
        {
            SaveIni();
            gfLastIniSave = brn_time();
        }
    }

    BRN_EXPORT("brn_mod_shutdown") void brn_mod_shutdown(void)
    {
        SaveIni();
        brn_set_input_capture(0);
    }
}
