﻿#include "stdafx.h"

#include "Headers/PanelManager.h"
#include "Headers/PanelManagerPane.h"
#include "Headers/TabbedWindow.h"

#include "Editor/Headers/Editor.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include <imgui_internal.h>

#ifndef STBI_INCLUDE_STB_IMAGE_H
#ifndef IMGUI_NO_STB_IMAGE_STATIC
#define STB_IMAGE_STATIC
#endif //IMGUI_NO_STB_IMAGE_STATIC
#ifndef IMGUI_NO_STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif //IMGUI_NO_STB_IMAGE_IMPLEMENTATION
#include "imgui/addons/imguibindings/stb_image.h"
#ifndef IMGUI_NO_STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_IMPLEMENTATION
#endif //IMGUI_NO_STB_IMAGE_IMPLEMENTATION
#ifndef IMGUI_NO_STB_IMAGE_STATIC
#undef STB_IMAGE_STATIC
#endif //IMGUI_NO_STB_IMAGE_STATIC
#endif //STBI_INCLUDE_STB_IMAGE_H

namespace Divide {

    ResourceCache* PanelManager::s_globalCache = nullptr;
    hashMapImpl<U32, Texture_ptr> PanelManager::s_imageEditorCache;

    static void DrawDockedWindows(ImGui::PanelManagerWindowData& wd) {
        reinterpret_cast<PanelManager*>(wd.userData)->drawDockedWindows(wd);
    }

    namespace {
        ImGui::TabWindow tabWindows[5];

        static const char* tabWindowNames[4] = { "TabWindow Left", "TabWindow Right", "TabWindow Top", "TabWindow Bottom" };
        static bool showDebugWindow = false;
        static bool showSampleWindow = false;

        bool show_test_window = true;
        bool show_another_window = false;
        I32 window_opacity = 255;
        I32 previous_window_opacity = 255;

        static const ImVec2 buttonSize(8, 10);
        static const ImVec2 buttonSizeSq(10, 10);

        static const char* DockedWindowNames[] = { "Solution Explorer","Toolbox","Property Window","Find Window","Output Window","Application Output","Preferences" };
        static const char* ToggleWindowNames[] = { "Toggle Window 1","Toggle Window 2","Toggle Window 3","Toggle Window 4" };

        void TabContentProvider(ImGui::TabWindow::TabLabel* tab, ImGui::TabWindow& parent, void* userPtr) {
            PanelManager* mgr = reinterpret_cast<PanelManager*>(userPtr);
            DisplayWindow& appWindow = mgr->context().activeWindow();

            // Users will use tab->userPtr here most of the time
            ImGui::Spacing();ImGui::Separator();
            if (tab) {
                ImGui::PushID(tab);
                if (tab->matchLabel("TabLabelStyle")) {
                    /*// Color Mode
                    static int colorEditMode = ImGuiColorEditMode_RGB;
                    static const char* btnlbls[2]={"HSV##myColorBtnType1","RGB##myColorBtnType1"};
                    if (colorEditMode!=ImGuiColorEditMode_RGB)  {
                    if (ImGui::SmallButton(btnlbls[0])) {
                    colorEditMode = ImGuiColorEditMode_RGB;
                    ImGui::ColorEditMode(colorEditMode);
                    }
                    }
                    else if (colorEditMode!=ImGuiColorEditMode_HSV)  {
                    if (ImGui::SmallButton(btnlbls[1])) {
                    colorEditMode = ImGuiColorEditMode_HSV;
                    ImGui::ColorEditMode(colorEditMode);
                    }
                    }
                    ImGui::SameLine(0);ImGui::Text("Color Mode");
                    ImGui::Separator();*/
                    ImGui::Spacing();
                    //ImGui::ColorEditMode(colorEditMode);
                    bool changed = ImGui::TabLabelStyle::Edit(ImGui::TabLabelStyle::Get());
                    ImGui::Separator();
                    const char* saveName = "tabLabelStyle.style";
                    const char* pSaveName = saveName;
                    if (ImGui::SmallButton("Save##saveGNEStyle1")) {
                        ImGui::TabLabelStyle::Save(ImGui::TabLabelStyle::Get(), pSaveName);
                        changed = false;tab->setModified(false);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Load##loadGNEStyle1")) {
                        ImGui::TabLabelStyle::Load(ImGui::TabLabelStyle::Get(), pSaveName);
                        changed = false;tab->setModified(false);
                    }
                    ImGui::SameLine();

                    if (ImGui::SmallButton("Reset##resetGNEStyle1")) {
                        ImGui::TabLabelStyle::Reset(ImGui::TabLabelStyle::Get());
                        changed = false;tab->setModified(false);
                    }

                    ImGui::Spacing();
                    if (changed) tab->setModified(true);
                }
                /*else if (tab->matchLabel("Render")) {
                // Just some experiments here
                ImGui::BeginChild("MyChildWindow",ImVec2(0,50),true);
                //if (ImGui::IsMouseDragging(0,1.f)) ImGui::SetTooltip("%s","Mouse Dragging");
                ImGui::EndChild();
                ImGui::BeginChild("MyChildWindow2",ImVec2(0,0),true);
                ImGui::Text("Here is the content of tab label: \"%s\"\n",tab->getLabel());
                ImGui::EndChild();
                }*/
                else if (tab->matchLabel("ImGuiMineGame")) {
#           if (defined(YES_IMGUIMINIGAMES) && !defined(NO_IMGUIMINIGAMES_MINE))
                    static ImGuiMiniGames::Mine mineGame;
                    mineGame.render();
#           else //NO_IMGUIMINIGAMES_MINE
                    ImGui::Text("Disabled for this build.");
#           endif // NO_IMGUIMINIGAMES_MINE
                } else if (tab->matchLabel("ImGuiSudokuGame")) {
#           if (defined(YES_IMGUIMINIGAMES) && !defined(NO_IMGUIMINIGAMES_SUDOKU))
                    static ImGuiMiniGames::Sudoku sudokuGame;
                    sudokuGame.render();
#           else // NO_IMGUIMINIGAMES_SUDOKU
                    ImGui::Text("Disabled for this build.");
#           endif // NO_IMGUIMINIGAMES_SUDOKU
                } else if (tab->matchLabel("ImGuiImageEditor")) {
#           ifdef YES_IMGUIIMAGEEDITOR
                    static ImGui::ImageEditor imageEditor;
                    stringImpl file = Paths::g_assetsLocation + Paths::g_GUILocation;
                    file.append("blankImage.png");

                    if (!imageEditor.isInited() && !imageEditor.loadFromFile(file.c_str())) {
                        //fprintf(stderr,"Loading \"./blankImage.png\" Failed.\n");
                    }
                    imageEditor.render();
                    tab->setModified(imageEditor.getModified());    // actually this should be automatic if we use the TabLabel extension approach inside our TabWindows (ImageEditor derives from TabLabel by default, but it's not tested)
#           else //YES_IMGUIIMAGEEDITOR
                    ImGui::Text("Disabled for this build.");
#           endif //YES_IMGUIIMAGEEDITOR
                } else if (tab->matchLabel("Scene")) {
                    ImVec2 size(0.0f, 0.0f);
                    ImGuiWindow* window = ImGui::GetCurrentWindow();

                    const RenderTarget& rt = mgr->context().gfx().renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::EDITOR));
                    const Texture_ptr& gameView = rt.getAttachment(RTAttachmentType::Colour, 0).texture();

                    int w = (int)gameView->getWidth();
                    int h = (int)gameView->getHeight();

                    if (window && !window->SkipItems && h > 0) {

                        float zoom = 0.85f;
                        ImVec2 curPos = ImGui::GetCursorPos();
                        const ImVec2 wndSz(size.x > 0 ? size.x : ImGui::GetWindowSize().x - curPos.x, size.y > 0 ? size.y : ImGui::GetWindowSize().y - curPos.y);
                        IM_ASSERT(wndSz.x != 0 && wndSz.y != 0 && zoom != 0);

                        ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + wndSz.x, window->DC.CursorPos.y + wndSz.y));
                        ImGui::ItemSize(bb);
                        if (ImGui::ItemAdd(bb, NULL)) {

                            ImVec2 imageSz = wndSz - ImVec2(0.2f, 0.2f);
                            ImVec2 remainingWndSize(0, 0);
                            const float aspectRatio = (float)w / (float)h;

                            if (aspectRatio != 0) {
                                const float wndAspectRatio = wndSz.x / wndSz.y;
                                if (aspectRatio >= wndAspectRatio) {
                                    imageSz.y = imageSz.x / aspectRatio;
                                    remainingWndSize.y = wndSz.y - imageSz.y;
                                } else {
                                    imageSz.x = imageSz.y*aspectRatio;
                                    remainingWndSize.x = wndSz.x - imageSz.x;
                                }
                            }

                            const float zoomFactor = .5f / zoom;
                            ImVec2 uvExtension = ImVec2(2.f*zoomFactor, 2.f*zoomFactor);
                            if (remainingWndSize.x > 0) {
                                const float remainingSizeInUVSpace = 2.f*zoomFactor*(remainingWndSize.x / imageSz.x);
                                const float deltaUV = uvExtension.x;
                                const float remainingUV = 1.f - deltaUV;
                                if (deltaUV<1) {
                                    float adder = (remainingUV < remainingSizeInUVSpace ? remainingUV : remainingSizeInUVSpace);
                                    uvExtension.x += adder;
                                    remainingWndSize.x -= adder * zoom * imageSz.x;
                                    imageSz.x += adder * zoom * imageSz.x;
                                }
                            }
                            if (remainingWndSize.y > 0) {
                                const float remainingSizeInUVSpace = 2.f*zoomFactor*(remainingWndSize.y / imageSz.y);
                                const float deltaUV = uvExtension.y;
                                const float remainingUV = 1.f - deltaUV;
                                if (deltaUV<1) {
                                    float adder = (remainingUV < remainingSizeInUVSpace ? remainingUV : remainingSizeInUVSpace);
                                    uvExtension.y += adder;
                                    remainingWndSize.y -= adder * zoom * imageSz.y;
                                    imageSz.y += adder * zoom * imageSz.y;
                                }
                            }


                            ImVec2 startPos = bb.Min, endPos = bb.Max;
                            startPos.x += remainingWndSize.x*.5f;
                            startPos.y += remainingWndSize.y*.5f;
                            endPos.x = startPos.x + imageSz.x;
                            endPos.y = startPos.y + imageSz.y;

                            const ImGuiID id = (ImGuiID)((U64)mgr) + 1;
                            ImGui::PushID(id);

                            bool sceneHovered = false;
                            ImGui::ButtonBehavior(ImRect(startPos, endPos), id, &sceneHovered, NULL);

                            window->DrawList->AddImage((void *)(intptr_t)gameView->getHandle(), startPos, endPos, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

                            Attorney::EditorPanelManager::setScenePreviewRect(mgr->context().editor(), vec4<I32>(startPos.x, startPos.y, endPos.x, endPos.y), sceneHovered);

                            ImGui::PopID();
                        }
                    }
                } else if (tab->matchLabel("ImGuiStyleChooser")) {
#           ifndef IMGUISTYLESERIALIZER_H_
                    ImGui::Text("GUI Style Chooser Disabled for this build.");
                    ImGui::Spacing();ImGui::Separator();
#           endif //IMGUISTYLESERIALIZER_H_

                    ImGui::Spacing();
                    ImGui::TextDisabled("%s", "Some controls to chenge the GUI style:");
                    ImGui::PushItemWidth(275);
                    if (ImGui::DragFloat("Global Font Scale", &ImGui::GetIO().FontGlobalScale, 0.005f, 0.3f, 2.0f, "%.2f")) mgr->setPanelManagerBoundsToIncludeMainMenuIfPresent();  // This is because the Main Menu height changes with the Font Scale
                    ImGui::PopItemWidth();
                    if (ImGui::GetIO().FontGlobalScale != 1.f) {
                        ImGui::SameLine(0, 10);
                        if (ImGui::SmallButton("Reset##glFontGlobalScale")) {
                            ImGui::GetIO().FontGlobalScale = 1.f;
                            mgr->setPanelManagerBoundsToIncludeMainMenuIfPresent();  // This is because the Main Menu height changes with the Font Scale
                        }
                    }
                    ImGui::Spacing();

                    ImGui::PushItemWidth(275);
                    ImGui::ColorEdit4("glClearColor", appWindow.clearColour()._v);
                    ImGui::PopItemWidth();
                    if (appWindow.clearColour() != appWindow.originalClearColour()) {
                        ImGui::SameLine(0, 10);
                        if (ImGui::SmallButton("Reset##glClearColorReset")) appWindow.clearColour(appWindow.originalClearColour());
                    }
                    ImGui::Spacing();

#           ifdef IMGUISTYLESERIALIZER_H_
                    static int styleEnumNum = 0;
                    ImGui::PushItemWidth(ImGui::GetWindowWidth()*0.44f);
                    if (ImGui::Combo("Main Style Chooser", &styleEnumNum, ImGui::GetDefaultStyleNames(), (int)ImGuiStyle_Count, (int)ImGuiStyle_Count)) {
                        ImGui::ResetStyle(styleEnumNum);
                    }
                    ImGui::PopItemWidth();
                    if (ImGui::IsItemHovered()) {
                        if (styleEnumNum == ImGuiStyle_Default)      ImGui::SetTooltip("%s", "\"Default\"\nThis is the default\nclassic ImGui theme");
                        else if (styleEnumNum == ImGuiStyle_DefaultDark)      ImGui::SetTooltip("%s", "\"DefaultDark\"\nThis is the default\ndark ImGui theme");
                        else if (styleEnumNum == ImGuiStyle_Gray)   ImGui::SetTooltip("%s", "\"Gray\"\nThis is the default theme of first demo");
                        else if (styleEnumNum == ImGuiStyle_OSX)   ImGui::SetTooltip("%s", "\"OSX\"\nPosted by @itamago here:\nhttps://github.com/ocornut/imgui/pull/511\n(hope I can use it)");
                        else if (styleEnumNum == ImGuiStyle_DarkOpaque)   ImGui::SetTooltip("%s", "\"DarkOpaque\"\nA dark-grayscale style with\nno transparency (by default)");
                        else if (styleEnumNum == ImGuiStyle_OSXOpaque)   ImGui::SetTooltip("%s", "\"OSXOpaque\"\nPosted by @dougbinks here:\nhttps://gist.github.com/dougbinks/8089b4bbaccaaf6fa204236978d165a9\n(hope I can use it)");
                        else if (styleEnumNum == ImGuiStyle_Soft) ImGui::SetTooltip("%s", "\"Soft\"\nPosted by @olekristensen here:\nhttps://github.com/ocornut/imgui/issues/539\n(hope I can use it)");
                        else if (styleEnumNum == ImGuiStyle_EdinBlack || styleEnumNum == ImGuiStyle_EdinWhite) ImGui::SetTooltip("%s", "Based on an image posted by @edin_p\n(hope I can use it)");
                        else if (styleEnumNum == ImGuiStyle_Maya) ImGui::SetTooltip("%s", "\"Maya\"\nPosted by @ongamex here:\nhttps://gist.github.com/ongamex/4ee36fb23d6c527939d0f4ba72144d29\n(hope I can use it)");
                    }
                    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
#           endif // IMGUISTYLESERIALIZER_H_

                    ImGui::TextDisabled("%s", "These are also present in the \"Preferences\" Panel:");
                    ImGui::DragFloat("Window Alpha##WA2", &mgr->ImGuiPanelManager().getDockedWindowsAlpha(), 0.005f, -0.01f, 1.0f, mgr->ImGuiPanelManager().getDockedWindowsAlpha() < 0.0f ? "(default)" : "%.3f");


                } else ImGui::Text("Here is the content of tab label: \"%s\"\n", tab->getLabel());
                ImGui::PopID();
            } else { ImGui::Text("EMPTY TAB LABEL DOCKING SPACE.");ImGui::Text("PLEASE DRAG AND DROP TAB LABELS HERE!"); }
            ImGui::Separator();ImGui::Spacing();
        }

        void TabLabelPopupMenuProvider(ImGui::TabWindow::TabLabel* tab, ImGui::TabWindow& parent, void* userPtr) {
            if (ImGui::BeginPopup(ImGui::TabWindow::GetTabLabelPopupMenuName())) {
                ImGui::PushID(tab);
                ImGui::Text("\"%.*s\" Menu", (int)(strlen(tab->getLabel()) - (tab->getModified() ? 1 : 0)), tab->getLabel());
                ImGui::Separator();
                if (!tab->matchLabel("TabLabelStyle") && !tab->matchLabel("Style")) {
                    if (tab->getModified()) { if (ImGui::MenuItem("Mark as not modified")) tab->setModified(false); } else { if (ImGui::MenuItem("Mark as modified"))     tab->setModified(true); }
                    ImGui::Separator();
                }
                ImGui::MenuItem("Entry 1");
                ImGui::MenuItem("Entry 2");
                ImGui::MenuItem("Entry 3");
                ImGui::MenuItem("Entry 4");
                ImGui::MenuItem("Entry 5");
                ImGui::PopID();
                ImGui::EndPopup();
            }

        }
        void TabLabelGroupPopupMenuProvider(ImVector<ImGui::TabWindow::TabLabel*>& tabs, ImGui::TabWindow& parent, ImGui::TabWindowNode* tabNode, void* userPtr) {
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(ImGui::TabLabelStyle::Get().colors[ImGui::TabLabelStyle::Col_TabLabel]));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ImGui::TabLabelStyle::Get().colors[ImGui::TabLabelStyle::Col_TabLabelText]));
            if (ImGui::BeginPopup(ImGui::TabWindow::GetTabLabelGroupPopupMenuName())) {
                ImGui::Text("TabLabel Group Menu");
                ImGui::Separator();
                if (parent.isMergeble(tabNode) && ImGui::MenuItem("Merge with parent group")) parent.merge(tabNode); // Warning: this invalidates "tabNode" after the call
                if (ImGui::MenuItem("Close all tabs in this group")) {
                    for (int i = 0, isz = tabs.size();i<isz;i++) {
                        ImGui::TabWindow::TabLabel* tab = tabs[i];
                        if (tab->isClosable())  // otherwise even non-closable tabs will be closed
                        {
                            //parent.removeTabLabel(tab);
                            tab->mustCloseNextFrame = true;  // alternative way... this asks for saving if file is modified
                        }
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save all tabs in all groups")) { parent.saveAll(); }
                if (ImGui::MenuItem("Close all tabs in all groups")) {
                    // This methods fires a modal dialog if we have unsaved files. To prevent this, we can try calling: parent.saveAll(); before
                    parent.startCloseAllDialog(NULL, true);  // returns true when a modal dialog is stated
                                                             // Note that modal dialogs in ImGui DON'T return soon with the result.
                                                             // However we can't prevent their "Cancel" button, so that if we know that it is started (=return value above),
                                                             // then we can call ImGui::TabWindow::AreSomeDialogsOpen(); and, if the return value is false, it is safe to close the program.
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Save Layout")) {
                    TabbedWindow::saveToFile(&tabWindows[0], sizeof(tabWindows) / sizeof(tabWindows[0]));
                }

                if (ImGui::MenuItem("Load Layout")) {
                    TabbedWindow::loadFromFile(&tabWindows[0], sizeof(tabWindows) / sizeof(tabWindows[0]));
                }

                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(2);
        }

        static void ShowExampleMenuFile()
        {
            ImGui::MenuItem("(dummy menu)", NULL, false, false);
            if (ImGui::MenuItem("New")) {}
            if (ImGui::MenuItem("Open", "Ctrl+O")) {}
            if (ImGui::BeginMenu("Open Recent"))
            {
                ImGui::MenuItem("fish_hat.c");
                ImGui::MenuItem("fish_hat.inl");
                ImGui::MenuItem("fish_hat.h");
                if (ImGui::BeginMenu("More.."))
                {
                    ImGui::MenuItem("Hello");
                    ImGui::MenuItem("Sailor");
                    if (ImGui::BeginMenu("Recurse.."))
                    {
                        ShowExampleMenuFile();
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) {}
            if (ImGui::MenuItem("Save As..")) {}
            ImGui::Separator();
            if (ImGui::BeginMenu("Options"))
            {
                static bool enabled = true;
                ImGui::MenuItem("Enabled", "", &enabled);
                ImGui::BeginChild("child", ImVec2(0, 60), true);
                for (int i = 0; i < 10; i++)
                    ImGui::Text("Scrolling Text %d", i);
                ImGui::EndChild();
                static float f = 0.5f;
                ImGui::SliderFloat("Value", &f, 0.0f, 1.0f);
                ImGui::InputFloat("Input", &f, 0.1f);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Colors"))
            {
                for (int i = 0; i < ImGuiCol_COUNT; i++)
                    ImGui::MenuItem(ImGui::GetStyleColorName((ImGuiCol)i));
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Disabled", false)) // Disabled
            {
                IM_ASSERT(0);
            }
            if (ImGui::MenuItem("Checked", NULL, true)) {}
            if (ImGui::MenuItem("Quit", "Alt+F4")) {}
        }
        static void ShowExampleMenuBar(PanelManager& mgr, bool isMainMenu = false)
        {
            //static bool ids[2];
            //ImGui::PushID((const void*) isMainMenu ? &ids[0] : &ids[1]);
            const bool open = isMainMenu ? ImGui::BeginMainMenuBar() : ImGui::BeginMenuBar();
            if (open)
            {
                if (ImGui::BeginMenu("File"))
                {
                    ShowExampleMenuFile();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit"))
                {
                    if (ImGui::MenuItem("Undo", "CTRL+Z")) {}
                    if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {}  // Disabled item
                    ImGui::Separator();
                    if (ImGui::MenuItem("Cut", "CTRL+X")) {}
                    if (ImGui::MenuItem("Copy", "CTRL+C")) {}
                    if (ImGui::MenuItem("Paste", "CTRL+V")) {}
                    ImGui::Separator();

                    // Just a test to check/uncheck multiple checkItems without closing the Menu (with the RMB)
                    static bool booleanProps[3] = { true,false,true };
                    static const char* names[3] = { "Boolean Test 1","Boolean Test 2","Boolean Test 3" };
                    const bool isRightMouseButtonClicked = ImGui::IsMouseClicked(1);    // cached
                    for (int i = 0;i<3;i++) {
                        ImGui::MenuItem(names[i], NULL, &booleanProps[i]);
                        if (isRightMouseButtonClicked && ImGui::IsItemHovered()) booleanProps[i] = !booleanProps[i];
                    }

                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Window"))
                {
                    if (ImGui::MenuItem("Debug Window", NULL, &showDebugWindow)) {
                        if (showDebugWindow && ImGui::IsItemHovered()) {
                            showDebugWindow = showDebugWindow;
                        }
                    }
                    if (ImGui::MenuItem("Sample Window", NULL, &showSampleWindow)) {
                        if (showSampleWindow && ImGui::IsItemHovered()) {
                            showSampleWindow = showSampleWindow;
                        }
                    }

                    ImGui::EndMenu();
                }
                if (isMainMenu) {
                    //gMainMenuBarSize = ImGui::GetWindowSize();
                    ImGui::EndMainMenuBar();
                } else ImGui::EndMenuBar();
            }
            //ImGui::PopID();
        }

        ImTextureID LoadTextureFromMemory(Texture_ptr& target, const stringImpl& name, int width, int height, int channels, const unsigned char* pixels, bool useMipmapsIfPossible, bool wraps, bool wrapt, bool minFilterNearest, bool magFilterNearest) {
        
            CLAMP(channels, 1, 4);

            ImTextureID texId = NULL;

            SamplerDescriptor sampler;
            sampler.setMinFilter(minFilterNearest ? TextureFilter::NEAREST : TextureFilter::LINEAR);
            sampler.setMagFilter(magFilterNearest ? TextureFilter::NEAREST : TextureFilter::LINEAR);
            sampler.setWrapModeU(wraps ? TextureWrap::REPEAT : TextureWrap::CLAMP);
            sampler.setWrapModeV(wrapt ? TextureWrap::REPEAT : TextureWrap::CLAMP);

            TextureDescriptor descriptor(TextureType::TEXTURE_2D,
                                         channels > 3 ? GFXImageFormat::RGBA8 :
                                                        channels > 2 ? GFXImageFormat::RGB8 :
                                                                       channels > 1 ? GFXImageFormat::RG8 :
                                                                                      GFXImageFormat::RED,
                                         GFXDataFormat::UNSIGNED_BYTE);
            descriptor.setSampler(sampler);
            descriptor.toggleAutomaticMipMapGeneration(useMipmapsIfPossible);

            ResourceDescriptor textureDescriptor(name);

            textureDescriptor.setThreadedLoading(false);
            textureDescriptor.setFlag(true);
            textureDescriptor.setPropertyDescriptor(descriptor);

            target = CreateResource<Texture>(*PanelManager::s_globalCache, textureDescriptor);
            assert(target);

            Texture::TextureLoadInfo info;

            target->loadData(info, (bufferPtr)pixels, vec2<U16>(width, height));

            texId = reinterpret_cast<void*>((intptr_t)target->getHandle());

            return texId;
        }

        ImTextureID LoadTextureFromMemory(Texture_ptr& target, const stringImpl& name, const unsigned char* filenameInMemory, int filenameInMemorySize, int req_comp = 0) {
            int w, h, n;
            unsigned char* pixels = stbi_load_from_memory(filenameInMemory, filenameInMemorySize, &w, &h, &n, req_comp);
            if (!pixels) {
                return 0;
            }
            ImTextureID ret = LoadTextureFromMemory(target, name, w, h, req_comp > 0 ? req_comp : n, pixels, false, true, true, false, false);

            stbi_image_free(pixels);

            return ret;
        }
        void FreeTextureDelegate(ImTextureID& texid) {
            hashMapImpl<U32, Texture_ptr>::iterator it = PanelManager::s_imageEditorCache.find((U32)(intptr_t)texid);
            if (it != std::cend(PanelManager::s_imageEditorCache)) {
                PanelManager::s_imageEditorCache.erase(it);
            } else {
                // error
            }
        }

        void GenerateOrUpdateTexture(ImTextureID& imtexid, int width, int height, int channels, const unsigned char* pixels, bool useMipmapsIfPossible, bool wraps, bool wrapt, bool minFilterNearest, bool magFilterNearest) {
            Texture_ptr tex = nullptr;
            LoadTextureFromMemory(tex,
                                  Util::StringFormat("temp_edit_tex_%d", PanelManager::s_imageEditorCache.size()),
                                  width,
                                  height,
                                  channels,
                                  pixels,
                                  useMipmapsIfPossible,
                                  wraps,
                                  wrapt,
                                  minFilterNearest,
                                  magFilterNearest);
            if (tex != nullptr) {
                imtexid = reinterpret_cast<void*>((intptr_t)tex->getHandle());
                PanelManager::s_imageEditorCache[tex->getHandle()] = tex;
            }
        }
    };

    PanelManager::PanelManager(PlatformContext& context)
        : PlatformContextComponent(context),
          _deltaTime(0ULL),
          _simulationPaused(nullptr),
          _showMainMenuBar(nullptr),
          _showCentralWindow(nullptr),
          _manager(std::make_unique<ImGui::PanelManager>()),
          _saveFile(Paths::g_saveLocation + Paths::Editor::g_saveLocation + Paths::Editor::g_panelLayoutFile)
    {
    }


    PanelManager::~PanelManager()
    {
        s_imageEditorCache.clear();
    }

    bool PanelManager::saveToFile() {
        return ImGui::PanelManager::Save(*_manager, _saveFile.c_str());
    }

    bool PanelManager::loadFromFile() {
        return ImGui::PanelManager::Load(*_manager, _saveFile.c_str());
    }

    void PanelManager::idle() {
        CLAMP(window_opacity, 0, 255);
        if (window_opacity != previous_window_opacity) {
            context().activeWindow().opacity(to_U8(window_opacity));
            previous_window_opacity = window_opacity;
        }
    }


    void PanelManager::draw(const U64 deltaTime) {
        _deltaTime = deltaTime;
        if (_showMainMenuBar && *_showMainMenuBar) {
            ShowExampleMenuBar(*this, true);
        }

        if (_showCentralWindow && *_showCentralWindow) {
            const ImVec2& iqs = _manager->getCentralQuadSize();
            if (iqs.x>ImGui::GetStyle().WindowMinSize.x && iqs.y>ImGui::GetStyle().WindowMinSize.y) {
                ImGui::SetNextWindowPos(_manager->getCentralQuadPosition());
                ImGui::SetNextWindowSize(_manager->getCentralQuadSize());
                if (ImGui::Begin("Central Window", NULL, ImVec2(0, 0), _manager->getDockedWindowsAlpha(), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | _manager->getDockedWindowsExtraFlags() /*| ImGuiWindowFlags_NoBringToFrontOnFocus*/)) {
                    tabWindows[0].render(); // Must be called inside "its" window (and sets isInited() to false). [ChildWindows can't be used here (but they can be used inside Tab Pages). Basically all the "Central Window" must be given to 'tabWindow'.]
                }
                ImGui::End();
            }
        }

        if (showDebugWindow) {
            drawIMGUIDebug();
        }
        if (showSampleWindow) {
            drawIMGUISample();
        }

        // Here we render mgr (our ImGui::PanelManager)
        ImGui::PanelManagerPane* pressedPane = NULL;  // Optional
        int pressedPaneButtonIndex = -1;            // Optional
        if (_manager->render(&pressedPane, &pressedPaneButtonIndex)) {
            //const ImVec2& iqp = mgr.getCentralQuadPosition();
            //const ImVec2& iqs = mgr.getCentralQuadSize();
        }

        // (Optional) Some manual feedback to the user (actually I detect gpShowMainMenuBar pressures here too, but pleese read below...):
        if (pressedPane && pressedPaneButtonIndex != -1)
        {
            static const char* paneNames[] = { "LEFT","RIGHT","TOP","BOTTOM" };
            if (!pressedPane->getWindowName(pressedPaneButtonIndex)) {
                ImGui::Toolbutton* pButton = NULL;
                pressedPane->getButtonAndWindow(pressedPaneButtonIndex, &pButton);
                if (pButton->isToggleButton) {
                    if (pressedPane->pos == ImGui::PanelManager::TOP && pressedPaneButtonIndex == (int)pressedPane->getSize() - 2) {
                        // For this we could have just checked if *gpShowMainMenuBar had changed its value before and after mgr.render()...
                        setPanelManagerBoundsToIncludeMainMenuIfPresent();
                    }
                } 
            } else {
                ImGui::Toolbutton* pButton = NULL;
                pressedPane->getButtonAndWindow(pressedPaneButtonIndex, &pButton);
            }

        }
    }

    void PanelManager::init() {
        if (s_globalCache == nullptr) {
            s_globalCache = &context().kernel().resourceCache();
        }

        if (!ImGui::TabWindow::DockPanelIconTextureID) {
            int dockPanelImageBufferSize = 0;
            const unsigned char* dockPanelImageBuffer = ImGui::TabWindow::GetDockPanelIconImagePng(&dockPanelImageBufferSize);
            ImGui::TabWindow::DockPanelIconTextureID = 
                LoadTextureFromMemory(_textures[to_base(TextureUsage::Dock)],
                                      "Docking Texture",
                                      dockPanelImageBuffer,
                                      dockPanelImageBufferSize);
        }

        ImGui::TabWindow::SetWindowContentDrawerCallback(&TabContentProvider, this); // Mandatory
        ImGui::TabWindow::SetTabLabelPopupMenuDrawerCallback(&TabLabelPopupMenuProvider, this);  // Optional (if you need context-menu)
        ImGui::TabWindow::SetTabLabelGroupPopupMenuDrawerCallback(&TabLabelGroupPopupMenuProvider, this);    // Optional (fired when RMB is clicked on an empty spot in the tab area)
#   ifdef YES_IMGUIIMAGEEDITOR
        ImGui::ImageEditor::SetGenerateOrUpdateTextureCallback(&GenerateOrUpdateTexture);   // This will be called only with channels=3 or channels=4
        ImGui::ImageEditor::SetFreeTextureCallback(&FreeTextureDelegate);
#   endif
        // Here we setup mgr (our ImGui::PanelManager)
        if (_manager->isEmpty()) {
            SamplerDescriptor sampler;
            sampler.setMinFilter(TextureFilter::NEAREST);
            sampler.setAnisotropy(0);
            sampler.setWrapMode(TextureWrap::CLAMP);
            // splash shader doesn't do gamma correction since that's a post processing step
            // so fake gamma correction by loading an sRGB image as a linear one.
            sampler.toggleSRGBColourSpace(false);

            TextureDescriptor descriptor(TextureType::TEXTURE_2D);
            descriptor.setSampler(sampler);

            ResourceDescriptor texture("Panel Manager Texture 1");

            texture.setThreadedLoading(false);
            texture.setFlag(true);
            texture.setResourceName("Tile8x8.png");
            texture.setResourceLocation(Paths::g_assetsLocation + Paths::g_GUILocation);
            texture.setPropertyDescriptor(descriptor);

            _textures[to_base(TextureUsage::Tile)] = CreateResource<Texture>(*s_globalCache, texture);

            texture.setName("Panel Manager Texture 2");
            texture.setResourceName("myNumbersTexture.png");
            _textures[to_base(TextureUsage::Numbers)] = CreateResource<Texture>(*s_globalCache, texture);

            // Hp) All the associated windows MUST have an unique name WITHOUT using the '##' chars that ImGui supports
            void* myImageTextureVoid = reinterpret_cast<void*>((intptr_t)_textures[to_base(TextureUsage::Tile)]->getHandle());         // 8x8 tiles
            void* myImageTextureVoid2 = reinterpret_cast<void*>((intptr_t)_textures[to_base(TextureUsage::Numbers)]->getHandle());       // 3x3 tiles
            ImVec2 uv0(0, 0), uv1(0, 0);int tileNumber = 0;

            _toolbars[to_base(PanelPositions::LEFT)] = std::make_unique<PanelManagerPane>(*this, "LeftToolbar##foo", ImGui::PanelManager::LEFT);
            _toolbars[to_base(PanelPositions::RIGHT)] = std::make_unique<PanelManagerPane>(*this, "RightToolbar##foo", ImGui::PanelManager::RIGHT);
            _toolbars[to_base(PanelPositions::TOP)] = std::make_unique<PanelManagerPane>(*this, "TopToobar##foo", ImGui::PanelManager::TOP);
            _toolbars[to_base(PanelPositions::BOTTOM)] = std::make_unique<PanelManagerPane>(*this, "BottomToolbar##foo", ImGui::PanelManager::BOTTOM);

            // LEFT PANE
            {
                ImGui::PanelManager::Pane& pane = _toolbars[to_base(PanelPositions::LEFT)]->impl();
                // Here we add the "proper" docked buttons and windows:
                for (int i = 0;i < 3;i++) {
                    // Add to left pane the first 3 windows DrawDockedWindows[i], with Toolbuttons with the first 3 images of myImageTextureVoid (8x8 tiles):
                    tileNumber = i;uv0 = ImVec2((float)(tileNumber % 8) / 8.f, (float)(tileNumber / 8) / 8.f);uv1 = ImVec2(uv0.x + 1.f / 8.f, uv0.y + 1.f / 8.f);
                    pane.addButtonAndWindow(ImGui::Toolbutton(DockedWindowNames[i], myImageTextureVoid, uv0, uv1, buttonSize),         // the 1st arg of Toolbutton is only used as a text for the tooltip.
                                                    ImGui::PanelManagerPaneAssociatedWindow(DockedWindowNames[i], -1, &DrawDockedWindows));  //  the 1st arg of PanelManagerPaneAssociatedWindow is the name of the window
                }
                _toolbars[to_base(PanelPositions::LEFT)]->addTabWindowIfSupported(tabWindowNames);
                pane.addSeparator(48); // Note that a separator "eats" one toolbutton index as if it was a real button

                // Here we add two "automatic" toggle buttons (i.e. toolbuttons + associated windows): only the last args of Toolbutton change.
                tileNumber = 0;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonAndWindow(ImGui::Toolbutton(ToggleWindowNames[0], myImageTextureVoid2, uv0, uv1, buttonSizeSq, true, false),        // the 1st arg of Toolbutton is only used as a text for the tooltip.
                                            ImGui::PanelManagerPaneAssociatedWindow(ToggleWindowNames[0], -1, &DrawDockedWindows, this));              //  the 1st arg of PanelManagerPaneAssociatedWindow is the name of the window
                tileNumber = 1;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonAndWindow(ImGui::Toolbutton(ToggleWindowNames[1], myImageTextureVoid2, uv0, uv1, buttonSizeSq, true, false),        // the 1st arg of Toolbutton is only used as a text for the tooltip.
                                            ImGui::PanelManagerPaneAssociatedWindow(ToggleWindowNames[1], -1, &DrawDockedWindows, this));              //  the 1st arg of PanelManagerPaneAssociatedWindow is the name of the window
                pane.addSeparator(48); // Note that a separator "eats" one toolbutton index as if it was a real button


                // Here we add two "manual" toggle buttons (i.e. toolbuttons only):
                tileNumber = 0;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonOnly(ImGui::Toolbutton("Manual toggle button 1", myImageTextureVoid2, uv0, uv1, buttonSizeSq, true, false));
                tileNumber = 1;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonOnly(ImGui::Toolbutton("Manual toggle button 2", myImageTextureVoid2, uv0, uv1, buttonSizeSq, true, false));

 
                // Optional line to manually specify alignment as ImVec2 (by default Y alignment is 0.0f for LEFT and RIGHT panes, and X alignment is 0.5f for TOP and BOTTOM panes)
                // Be warned that the component that you don't use (X in this case, must be set to 0.f for LEFT or 1.0f for RIGHT, unless you want to do strange things)
                //pane.setToolbarProperties(true,false,ImVec2(0.f,0.5f));  // place this pane at Y center (instead of Y top)
            }
            // RIGHT PANE
            {
                ImGui::PanelManager::Pane& pane = _toolbars[to_base(PanelPositions::RIGHT)]->impl();
                // Here we use (a part of) the left pane to clone windows (handy since we don't support drag and drop):
                if (_manager->getPaneLeft()) pane.addClonedPane(*_manager->getPaneLeft(), false, 0, 2); // note that only the "docked" part of buttons/windows are clonable ("manual" buttons are simply ignored): TO FIX: for now please avoid leaving -1 as the last argument, as this seems to mess up button indices: just explicitely copy NonTogglable-DockButtons yourself.
                                                                                                // To clone single buttons (and not the whole pane) please use: pane.addClonedButtonAndWindow(...);
                                                                                                // IMPORTANT: Toggle Toolbuttons (and associated windows) can't be cloned and are just skipped if present
                _toolbars[to_base(PanelPositions::RIGHT)]->addTabWindowIfSupported(tabWindowNames);

                // here we could add new docked windows as well in the usual way now... but we don't
                pane.addSeparator(48);   // Note that a separator "eats" one toolbutton index as if it was a real button

                                            // Here we add two other "manual" toggle buttons:
                tileNumber = 2;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonOnly(ImGui::Toolbutton("Manual toggle button 3", myImageTextureVoid2, uv0, uv1, buttonSize, true, false));
                tileNumber = 3;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonOnly(ImGui::Toolbutton("Manual toggle button 4", myImageTextureVoid2, uv0, uv1, buttonSize, true, false));

                // Here we add two "manual" normal buttons (actually "normal" buttons are always "manual"):
                pane.addSeparator(48);   // Note that a separator "eats" one toolbutton index as if it was a real button
                tileNumber = 4;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonOnly(ImGui::Toolbutton("Manual normal button 1", myImageTextureVoid2, uv0, uv1, buttonSize, false, false));
                tileNumber = 5;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonOnly(ImGui::Toolbutton("Manual toggle button 2", myImageTextureVoid2, uv0, uv1, buttonSize, false, false));

            }
            // BOTTOM PANE
            {
                ImGui::PanelManager::Pane& pane = _toolbars[to_base(PanelPositions::BOTTOM)]->impl();
                // Here we add the "proper" docked buttons and windows:
                for (int i = 3;i < 6;i++) {
                    // Add to left pane the windows DrawDockedWindows[i] from 3 to 6, with Toolbuttons with the images from 3 to 6 of myImageTextureVoid (8x8 tiles):
                    tileNumber = i;uv0 = ImVec2((float)(tileNumber % 8) / 8.f, (float)(tileNumber / 8) / 8.f);uv1 = ImVec2(uv0.x + 1.f / 8.f, uv0.y + 1.f / 8.f);
                    pane.addButtonAndWindow(ImGui::Toolbutton(DockedWindowNames[i], myImageTextureVoid, uv0, uv1, buttonSizeSq),         // the 1st arg of Toolbutton is only used as a text for the tooltip.
                                                ImGui::PanelManagerPaneAssociatedWindow(DockedWindowNames[i], -1, &DrawDockedWindows, this));  //  the 1st arg of PanelManagerPaneAssociatedWindow is the name of the window
                }
                _toolbars[to_base(PanelPositions::BOTTOM)]->addTabWindowIfSupported(tabWindowNames);
                pane.addSeparator(64); // Note that a separator "eats" one toolbutton index as if it was a real button

                // Here we add two "automatic" toggle buttons (i.e. toolbuttons + associated windows): only the last args of Toolbutton change.
                tileNumber = 2;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonAndWindow(ImGui::Toolbutton(ToggleWindowNames[2], myImageTextureVoid2, uv0, uv1, buttonSizeSq, true, false),        // the 1st arg of Toolbutton is only used as a text for the tooltip.
                                            ImGui::PanelManagerPaneAssociatedWindow(ToggleWindowNames[2], -1, &DrawDockedWindows, this));              //  the 1st arg of PanelManagerPaneAssociatedWindow is the name of the window
                tileNumber = 3;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonAndWindow(ImGui::Toolbutton(ToggleWindowNames[3], myImageTextureVoid2, uv0, uv1, buttonSizeSq, true, false),        // the 1st arg of Toolbutton is only used as a text for the tooltip.
                                            ImGui::PanelManagerPaneAssociatedWindow(ToggleWindowNames[3], -1, &DrawDockedWindows, this));              //  the 1st arg of PanelManagerPaneAssociatedWindow is the name of the window
                pane.addSeparator(64); // Note that a separator "eats" one toolbutton index as if it was a real button

                // Here we add two "manual" toggle buttons:
                tileNumber = 4;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonOnly(ImGui::Toolbutton("Manual toggle button 4", myImageTextureVoid2, uv0, uv1, buttonSizeSq, true, false));
                tileNumber = 5;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonOnly(ImGui::Toolbutton("Manual toggle button 5", myImageTextureVoid2, uv0, uv1, buttonSizeSq, true, false));

            }
            // TOP PANE
            {

                ImGui::PanelManager::Pane& pane = _toolbars[to_base(PanelPositions::TOP)]->impl();
                // Here we add the "proper" docked buttons and windows:
                for (int i = 6;i < 7;i++) {
                    // Add to left pane the windows DrawDockedWindows[i] from 3 to 6, with Toolbuttons with the images from 3 to 6 of myImageTextureVoid (8x8 tiles):
                    tileNumber = i;uv0 = ImVec2((float)(tileNumber % 8) / 8.f, (float)(tileNumber / 8) / 8.f);uv1 = ImVec2(uv0.x + 1.f / 8.f, uv0.y + 1.f / 8.f);
                    pane.addButtonAndWindow(ImGui::Toolbutton(DockedWindowNames[i], myImageTextureVoid, uv0, uv1, buttonSizeSq),         // the 1st arg of Toolbutton is only used as a text for the tooltip.
                                                ImGui::PanelManagerPaneAssociatedWindow(DockedWindowNames[i], -1, &DrawDockedWindows, this));  //  the 1st arg of PanelManagerPaneAssociatedWindow is the name of the window
                }
                _toolbars[to_base(PanelPositions::TOP)]->addTabWindowIfSupported(tabWindowNames);
                pane.addSeparator(64); // Note that a separator "eats" one toolbutton index as if it was a real button

                pane.addButtonOnly(ImGui::Toolbutton("Normal Manual Button 1", myImageTextureVoid2, ImVec2(0, 0), ImVec2(1.f / 3.f, 1.f / 3.f), buttonSizeSq));//,false,false,ImVec4(0,1,0,1)));  // Here we add a free button
                tileNumber = 1;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonOnly(ImGui::Toolbutton("Normal Manual Button 2", myImageTextureVoid2, uv0, uv1, buttonSizeSq));  // Here we add a free button
                tileNumber = 2;uv0 = ImVec2((float)(tileNumber % 3) / 3.f, (float)(tileNumber / 3) / 3.f);uv1 = ImVec2(uv0.x + 1.f / 3.f, uv0.y + 1.f / 3.f);
                pane.addButtonOnly(ImGui::Toolbutton("Normal Manual Button 3", myImageTextureVoid2, uv0, uv1, buttonSizeSq));  // Here we add a free button
                pane.addSeparator(32);  // Note that a separator "eats" one toolbutton index as if it was a real button

                // Here we add two manual toggle buttons, but we'll use them later to show/hide menu and show/hide a central window
                tileNumber = 51;uv0 = ImVec2((float)(tileNumber % 8) / 8.f, (float)(tileNumber / 8) / 8.f);uv1 = ImVec2(uv0.x + 1.f / 8.f, uv0.y + 1.f / 8.f);
                pane.addButtonOnly(ImGui::Toolbutton("Show/Hide Main Menu Bar", myImageTextureVoid, uv0, uv1, buttonSizeSq, true, true));  // [*] Here we add a manual toggle button we'll simply bind to "gpShowMainMenuBar" later. Start value is last arg.
                tileNumber = 5;uv0 = ImVec2((float)(tileNumber % 8) / 8.f, (float)(tileNumber / 8) / 8.f);uv1 = ImVec2(uv0.x + 1.f / 8.f, uv0.y + 1.f / 8.f);
                pane.addButtonOnly(ImGui::Toolbutton("Show/Hide central window", myImageTextureVoid, uv0, uv1, buttonSizeSq, true, true));  // [**] Here we add a manual toggle button we'll process later [**]

                // Ok. Now all the buttons/windows have been added to the TOP Pane.
                tileNumber = 34;uv0 = ImVec2((float)(tileNumber % 8) / 8.f, (float)(tileNumber / 8) / 8.f);uv1 = ImVec2(uv0.x + 1.f / 8.f, uv0.y + 1.f / 8.f);
                pane.addButtonOnly(ImGui::Toolbutton("Play/Pause", myImageTextureVoid, uv0, uv1, buttonSize, true, false, ImVec4(1,0,0,1)));

                // Please note that it's not safe to add EVERYTHING (even separators) to this (TOP) pane afterwards (unless we bind the booleans again).
                _showMainMenuBar = &pane.bar.getButton(pane.getSize() - 3)->isDown;            // [*]
                _showCentralWindow = &pane.bar.getButton(pane.getSize() - 2)->isDown;          // [**]
                _simulationPaused = &pane.bar.getButton(pane.getSize() - 1)->isDown;           // [***]

            }

            // Optional. Loads the layout (just selectedButtons, docked windows sizes and stuff like that)
            loadFromFile();

            _manager->setDockedWindowsAlpha(1.0f);

            // Optional line that affects the look of all the Toolbuttons in the Panes inserted so far:
            _manager->overrideAllExistingPanesDisplayProperties(ImVec2(0.25f, 0.9f), ImVec4(0.85, 0.85, 0.85, 1));

            // These line is only necessary to accommodate space for the global menu bar we're using:
            setPanelManagerBoundsToIncludeMainMenuIfPresent();
        }

        // The following block used to be in DrawGL(), but it's better to move it here (it's part of the initalization)
        // Here we load all the Tabs, if their config file is available
        // Otherwise we create some Tabs in the central tabWindow (tabWindows[0])
        ImGui::TabWindow& tabWindow = tabWindows[0];
        if (!tabWindow.isInited()) {
            // tabWindow.isInited() becomes true after the first call to tabWindow.render() [in DrawGL()]
            for (int i = 0;i < 5;i++) {
                tabWindows[i].clear();  // for robustness (they're already empty)
            }

            if (!TabbedWindow::loadFromFile(tabWindows, sizeof(tabWindows) / sizeof(tabWindows[0]))) {
                // Here we set the starting configurations of the tabs in our ImGui::TabWindows
                static const char* tabNames[] = { "TabLabelStyle","Render","Layers","Capture","Scene","World","Object","Constraints","Modifiers","Data","Material","Texture","Particle","Physics" };
                static const int numTabs = sizeof(tabNames) / sizeof(tabNames[0]);
                static const char* tabTooltips[numTabs] = { "Edit the look of the tab labels","Render Tab Tooltip","Layers Tab Tooltip","Capture Tab Tooltip","non-draggable","Another Tab Tooltip","","","","non-draggable","Tired to add tooltips...","" };
                for (int i = 0;i<numTabs;i++) {
                    tabWindow.addTabLabel(tabNames[i], tabTooltips[i], i % 3 != 0, i % 5 != 4);
                }
#       ifdef YES_IMGUIMINIGAMES
#           ifndef NO_IMGUIMINIGAMES_MINE
                tabWindow.addTabLabel("ImGuiMineGame", "a mini-game", false, true, NULL, NULL, 0, ImGuiWindowFlags_NoScrollbar);
#           endif // NO_IMGUIMINIGAMES_MINE
#           ifndef NO_IMGUIMINIGAMES_SUDOKU
                tabWindow.addTabLabel("ImGuiSudokuGame", "a mini-game", false, true, NULL, NULL, 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
#           endif // NO_IMGUIMINIGAMES_SUDOKU
#       endif // YES_IMGUIMINIGAMES
#       ifdef YES_IMGUIIMAGEEDITOR
                tabWindow.addTabLabel("ImGuiImageEditor", "a tiny image editor", false, true, NULL, NULL, 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
#       endif //YES_IMGUIIMAGEEDITOR
                tabWindow.addTabLabel("ImGuiStyleChooser", "Edit the look of the GUI", false);
            }
        }
    }

    void PanelManager::destroy() {

    }


    // Here are two static methods useful to handle the change of size of the togglable mainMenu we will use
    // Returns the height of the main menu based on the current font (from: ImGui::CalcMainMenuHeight() in imguihelper.h)
    float PanelManager::calcMainMenuHeight() {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStyle& style = ImGui::GetStyle();
        ImFont* font = ImGui::GetFont();
        if (!font) {
            if (io.Fonts->Fonts.size()>0) font = io.Fonts->Fonts[0];
            else return (14) + style.FramePadding.y * 2.0f;
        }
        return (io.FontGlobalScale * font->Scale * font->FontSize) + style.FramePadding.y * 2.0f;
    }

    void  PanelManager::setPanelManagerBoundsToIncludeMainMenuIfPresent(int displayX, int displayY) {
        if (_showMainMenuBar) {
            if (displayX <= 0) displayX = ImGui::GetIO().DisplaySize.x;
            if (displayY <= 0) displayY = ImGui::GetIO().DisplaySize.y;
            ImVec4 bounds(0, 0, (float)displayX, (float)displayY);   // (0,0,-1,-1) defaults to (0,0,io.DisplaySize.x,io.DisplaySize.y)
            if (*_showMainMenuBar) {
                const float mainMenuHeight = calcMainMenuHeight();
                bounds = ImVec4(0, mainMenuHeight, displayX, displayY - mainMenuHeight);
            }
            _manager->setDisplayPortion(bounds);
        }
    }

    void PanelManager::resize(int w, int h) {
        static ImVec2 initialSize(w, h);
        _manager->setToolbarsScaling((float)w / initialSize.x, (float)h / initialSize.y);  // Scales the PanelManager bounmds based on the initialSize
        setPanelManagerBoundsToIncludeMainMenuIfPresent(w, h);                  // This line is only necessary if we have a global menu bar
    }

    void PanelManager::drawIMGUISample() {
        ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiCond_FirstUseEver);
        ImGui::ShowTestWindow(&show_test_window);
    }

    void PanelManager::drawIMGUIDebug() {
        DisplayWindow& window = context().activeWindow();

        static float f = 0.0f;
        ImGui::Text("Hello, world!");
        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
        ImGui::SliderInt("Opacity", &window_opacity, 0, 255);
        ImGui::ColorEdit4("clear color", window.clearColour()._v);
        if (ImGui::Button("Test Window")) show_test_window ^= 1;
        if (ImGui::Button("Another Window")) show_another_window ^= 1;
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::Text("Time since last frame %.3f ms", Time::MicrosecondsToMilliseconds<float>(_deltaTime));
        if (ImGui::Button("Toggle cursor")) {
            ImGuiIO& io = ImGui::GetIO();
            io.MouseDrawCursor = !io.MouseDrawCursor;
        }
        // 2. Show another simple window. In most cases you will use an explicit Begin/End pair to name the window.
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);
            ImGui::Text("Hello from another window!");
            ImGui::End();
        }
    }

    void PanelManager::drawDockedWindows(ImGui::PanelManagerWindowData& wd) {
        if (!wd.isToggleWindow) {
            // Here we simply draw all the docked windows (in our case DockedWindowNames) without using ImGui::Begin()/ImGui::End().
            // (This is necessary because docked windows are not normal windows: see the title bar for example)
            if (strcmp(wd.name, DockedWindowNames[0]) == 0) {
                // Draw Solution Explorer
                ImGui::Text("%s\n", wd.name);
                static float f;
                ImGui::Text("Hello, world!");
                ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
                //show_test_window ^= ImGui::Button("Test Window");
                //show_another_window ^= ImGui::Button("Another Window");

                // Calculate and show framerate
                static float ms_per_frame[120] = { 0 };
                static int ms_per_frame_idx = 0;
                static float ms_per_frame_accum = 0.0f;
                ms_per_frame_accum -= ms_per_frame[ms_per_frame_idx];
                ms_per_frame[ms_per_frame_idx] = ImGui::GetIO().DeltaTime * 1000.0f;
                ms_per_frame_accum += ms_per_frame[ms_per_frame_idx];
                ms_per_frame_idx = (ms_per_frame_idx + 1) % 120;
                const float ms_per_frame_avg = ms_per_frame_accum / 120;
                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", ms_per_frame_avg, 1000.0f / ms_per_frame_avg);
            } else if (strcmp(wd.name, DockedWindowNames[1]) == 0) {
                // Draw Toolbox
                ImGui::Text("%s\n", wd.name);
            } else if (strcmp(wd.name, DockedWindowNames[2]) == 0) {
                ImGui::Text("%s\n", wd.name);
#           ifdef TEST_ICONS_INSIDE_TTF
                ImGui::Spacing();ImGui::Separator();
                ImGui::TextDisabled("Testing icons inside FontAwesome");
                ImGui::Separator();ImGui::Spacing();

                ImGui::Button(ICON_FA_FILE "  File"); // use string literal concatenation, ouputs a file icon and File
#           ifndef NO_IMGUIFILESYSTEM // Testing icons inside ImGuiFs::Dialog
                ImGui::AlignFirstTextHeightToWidgets();ImGui::Text("File:");ImGui::SameLine();
                static ImGuiFs::Dialog dlg;
                ImGui::InputText("###fsdlg", (char*)dlg.getChosenPath(), ImGuiFs::MAX_PATH_BYTES, ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                const bool browseButtonPressed = ImGui::Button("...##fsdlg");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "file chooser dialog\nwith FontAwesome icons");
                dlg.chooseFileDialog(browseButtonPressed, dlg.getLastDirectory());
#           endif //NO_IMGUIFILESYSTEM
                if (ImGui::TreeNode("All FontAwesome Icons")) {
                    DrawAllFontAwesomeIcons();
                    ImGui::TreePop();
                }

                ImGui::Spacing();ImGui::Separator();
                ImGui::TextDisabled("Testing styled check boxes by dougbinks");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "https://gist.github.com/dougbinks/8089b4bbaccaaf6fa204236978d165a9\nThey work better with a Monospace font");
                ImGui::Separator();ImGui::Spacing();

                static bool cb1 = false;ImGui::CheckBoxFont("CheckBoxFont", &cb1);
                static bool cb2 = false;ImGui::CheckBoxTick("CheckBoxTick", &cb2);
                static bool cb3 = false;ImGui::MenuItemCheckBox("MenuItemCheckBox", &cb3);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Probably this works only\ninside a Menu...");


                // We, we can use default fonts as well (std UTF8 chars):
                ImGui::Spacing();ImGui::Separator();
                ImGui::TextDisabled("Testing the same without any icon font");
                ImGui::Separator();ImGui::Spacing();

                static bool cb4 = false;ImGui::CheckBoxFont("CheckBoxDefaultFonts", &cb4, "▣", "□");
                static bool cb5 = false;ImGui::CheckBoxFont("CheckBoxDefaultFonts2", &cb5, "■", "□");
                static bool cb6 = false;ImGui::CheckBoxFont("CheckBoxDefaultFonts3", &cb6, "▼", "▶");
                static bool cb7 = false;ImGui::CheckBoxFont("CheckBoxDefaultFonts4", &cb7, "▽", "▷");
#           endif //TEST_ICONS_INSIDE_TTF
            } else if (strcmp(wd.name, DockedWindowNames[3]) == 0) {
                // Draw Find Window
                ImGui::Text("%s\n", wd.name);
            } else if (strcmp(wd.name, DockedWindowNames[4]) == 0) {
                // Draw Output Window
                Attorney::EditorPanelManager::drawOutputWindow(context().editor());
            } else if (strcmp(wd.name, "Preferences") == 0) {
                ImGui::DragFloat("Window Alpha##WA1", &_manager->getDockedWindowsAlpha(), 0.005f, -0.01f, 1.0f, _manager->getDockedWindowsAlpha() < 0.0f ? "(default)" : "%.3f");

                bool noTitleBar = _manager->getDockedWindowsNoTitleBar();
                if (ImGui::Checkbox("No Window TitleBars", &noTitleBar)) _manager->setDockedWindowsNoTitleBar(noTitleBar);
                if (_showCentralWindow) {
                    ImGui::SameLine();ImGui::Checkbox("Show Central Wndow", (bool*)_showCentralWindow);
                }
                if (_showMainMenuBar) {
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Show Main Menu", (bool*)_showMainMenuBar)) setPanelManagerBoundsToIncludeMainMenuIfPresent();
                }
                // Here we test saving/loading the ImGui::PanelManager layout (= the sizes of the 4 docked windows and the buttons that are selected on the 4 toolbars)
                // Please note that the API should allow loading/saving different items into a single file and loading/saving from/to memory too, but we don't show it now.

                ImGui::Separator();
                static const char pmTooltip[] = "the ImGui::PanelManager layout\n(the sizes of the 4 docked windows and\nthe buttons that are selected\non the 4 toolbars)";
                if (ImGui::Button("Save Panel Manager Layout")) {
                    saveToFile();
                }

                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save %s", pmTooltip);
                ImGui::SameLine();
                if (ImGui::Button("Load Panel Manager Layout")) {
                    if (loadFromFile())
                        setPanelManagerBoundsToIncludeMainMenuIfPresent();	// That's because we must adjust gpShowMainMenuBar state here (we have used a manual toggle button for it)
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load %s", pmTooltip);


                ImGui::Separator();
                static const char twTooltip[] = "the layout of the 5 ImGui::TabWindows\n(this option is also available by right-clicking\non an empty space in the Tab Header)";
                if (ImGui::Button("Save TabWindows Layout")) {
                    TabbedWindow::saveToFile(tabWindows, sizeof(tabWindows) / sizeof(tabWindows[0]));
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save %s", twTooltip);
                ImGui::SameLine();
                if (ImGui::Button("Load TabWindows Layout")) {
                    TabbedWindow::loadFromFile(tabWindows, sizeof(tabWindows) / sizeof(tabWindows[0]));
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load %s", twTooltip);

                //ImGui::Spacing();
                //if (ImGui::Button("Reset Central Window Tabs")) ResetTabWindow(tabWindow);
            } else /*if (strcmp(wd.name,DockedWindowNames[5])==0)*/ {
                // Draw Application Window
                ImGui::Text("%s\n", wd.name);
            }
        } else {
            // Here we draw our toggle windows (in our case ToggleWindowNames) in the usual way:
            // We can use -1.f for alpha here, instead of mgr.getDockedWindowsAlpha(), that can be too low (but choose what you like)
            if (ImGui::Begin(wd.name, &wd.open, wd.size, -1.f, ImGuiWindowFlags_NoSavedSettings)) {
                if (strcmp(wd.name, ToggleWindowNames[0]) == 0) {
                    // Draw Toggle Window 1
                    ImGui::SetWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x*0.15f, ImGui::GetIO().DisplaySize.y*0.24f), ImGuiSetCond_FirstUseEver);
                    ImGui::SetWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x*0.25f, ImGui::GetIO().DisplaySize.y*0.24f), ImGuiSetCond_FirstUseEver);

                    ImGui::Text("Hello world from toggle window \"%s\"", wd.name);
                }
                else
                {
                    // Draw Toggle Window
                    ImGui::SetWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x*0.25f, ImGui::GetIO().DisplaySize.y*0.34f), ImGuiSetCond_FirstUseEver);
                    ImGui::SetWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x*0.5f, ImGui::GetIO().DisplaySize.y*0.34f), ImGuiSetCond_FirstUseEver);
                    ImGui::Text("Hello world from toggle window \"%s\"", wd.name);

                    //ImGui::Checkbox("wd.open",&wd.open);  // This can be used to close the window too
                }
            }
            ImGui::End();
        }
    }


    void PanelManager::drawDockedTabWindows(ImGui::PanelManagerWindowData& wd) {
        // See more generic DrawDockedWindows(...) below...
        ImGui::TabWindow& tabWindow = tabWindows[(wd.dockPos == ImGui::PanelManager::LEFT) ? 1 :
            (wd.dockPos == ImGui::PanelManager::RIGHT) ? 2 :
            (wd.dockPos == ImGui::PanelManager::TOP) ? 3
            : 4];
        tabWindow.render();
    }

}; //namespace Divide