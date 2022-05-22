#include "stdafx.h"

#include "Headers/GUI.h"
#include "Headers/SceneGUIElements.h"

#include "Headers/GUIButton.h"
#include "Headers/GUIConsole.h"
#include "Headers/GUIMessageBox.h"
#include "Headers/GUIText.h"

#include "Scenes/Headers/Scene.h"

#include "Core/Debugging/Headers/DebugInterface.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Audio/Headers/SFXDevice.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {
namespace {
    GUIMessageBox* g_assertMsgBox = nullptr;
};

void DIVIDE_ASSERT_MSG_BOX(const char* failMessage) noexcept {
    if_constexpr(Config::Assert::SHOW_MESSAGE_BOX) {
        if (g_assertMsgBox) {
            g_assertMsgBox->setTitle("Assertion Failed!");
            g_assertMsgBox->setMessage(failMessage);
            g_assertMsgBox->setMessageType(GUIMessageBox::MessageType::MESSAGE_ERROR);
            g_assertMsgBox->show();
        }
    }
}

GUI::GUI(Kernel& parent)
  : GUIInterface(*this),
    KernelComponent(parent),
    _ceguiInput(*this),
    _textRenderInterval(Time::MillisecondsToMicroseconds(10))
{
    // 500ms
    _ceguiInput.setInitialDelay(0.500f);
}

GUI::~GUI()
{
    destroy();
}

void GUI::onChangeScene(Scene* newScene) {
    assert(newScene != nullptr);
    SharedLock<SharedMutex> r_lock(_guiStackLock);
    if (_activeScene != nullptr && _activeScene->getGUID() != newScene->getGUID()) {
        const GUIMapPerScene::const_iterator it = _guiStack.find(_activeScene->getGUID());
        if (it != std::cend(_guiStack)) {
            it->second->onDisable();
        }
    }

    const GUIMapPerScene::const_iterator it = _guiStack.find(newScene->getGUID());
    if (it != std::cend(_guiStack)) {
        it->second->onEnable();
    } else {
        SceneGUIElements* elements = Attorney::SceneGUI::guiElements(*newScene);
        insert(_guiStack, newScene->getGUID(), elements);
        elements->onEnable();
    }

    _activeScene = newScene;
    recreateDefaultMessageBox();
}

void GUI::onUnloadScene(Scene* const scene) {
    assert(scene != nullptr);
    ScopedLock<SharedMutex> w_lock(_guiStackLock);
    const GUIMapPerScene::const_iterator it = _guiStack.find(scene->getGUID());
    if (it != std::cend(_guiStack)) {
        _guiStack.erase(it);
    }
}

void GUI::CEGUIDrawInternal() {
    OPTICK_EVENT();

    const Configuration::GUI& guiConfig = parent().platformContext().config().gui;
    if (guiConfig.cegui.enabled) {
        _ceguiRenderer->beginRendering();
        _ceguiRenderTextureTarget->clear();
        _ceguiContext->draw();
        _ceguiRenderer->endRendering();
    }
}

void GUI::draw(GFXDevice& context, const Rect<I32>& viewport, GFX::CommandBuffer& bufferInOut) {
    if (!_init || !_activeScene) {
        return;
    }
    OPTICK_EVENT();

    EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Render GUI" });

    //Set a 2D camera for rendering
    EnqueueCommand(bufferInOut, GFX::SetCameraCommand{ Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->snapshot() });

    EnqueueCommand(bufferInOut, GFX::SetViewportCommand{ viewport });

    const GUIMap& elements = _guiElements[to_base(GUIType::GUI_TEXT)];

    TextElementBatch textBatch;
    textBatch.data().reserve(elements.size());
    for (const GUIMap::value_type& guiStackIterator : elements) {
        const GUIText& textLabel = static_cast<GUIText&>(*guiStackIterator.second.first);
        if (textLabel.visible() && !textLabel.text().empty()) {
            textBatch.data().push_back(textLabel);
        }
    }

    if (!textBatch.data().empty()) {
        Attorney::GFXDeviceGUI::drawText(context, textBatch, bufferInOut);
    }

    {
        SharedLock<SharedMutex> r_lock(_guiStackLock);
        // scene specific
        const GUIMapPerScene::const_iterator it = _guiStack.find(_activeScene->getGUID());
        if (it != std::cend(_guiStack)) {
            it->second->draw(context, bufferInOut);
        }
    }

    const Configuration::GUI& guiConfig = parent().platformContext().config().gui;

    if (guiConfig.cegui.enabled) {
        GFX::EnqueueCommand<GFX::ExternalCommand>(bufferInOut)->_cbk = [this]() { CEGUIDrawInternal(); };
        context.drawTextureInViewport(getCEGUIRenderTextureData(), 0u, viewport, false, false, true, bufferInOut);
    }

    // Restore full state
    GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline =  _postCEGUIPipeline;
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void GUI::idle() {
    NOP();
}

void GUI::update(const U64 deltaTimeUS) {
    OPTICK_EVENT();

    if (!_init) {
        return;
    }

    const Configuration::GUI& guiConfig = parent().platformContext().config().gui;
    if (guiConfig.cegui.enabled) {
        _ceguiInput.update(deltaTimeUS);
        auto& ceguiSystem = CEGUI::System::getSingleton();
        ceguiSystem.injectTimePulse(Time::MicrosecondsToSeconds<F32>(deltaTimeUS));
        ceguiSystem.getDefaultGUIContext().injectTimePulse(Time::MicrosecondsToSeconds<F32>(deltaTimeUS));
    }

    if (_console) {
        _console->update(deltaTimeUS);
    }
}

void GUI::setRenderer(CEGUI::Renderer& renderer) {
    const Configuration::GUI& guiConfig = parent().platformContext().config().gui;
    if (guiConfig.cegui.enabled) {
        CEGUI::System::create(renderer, nullptr, nullptr, nullptr, nullptr, "", (Paths::g_logPath + "CEGUI.log").c_str());

        if_constexpr(Config::Build::IS_DEBUG_BUILD) {
            CEGUI::Logger::getSingleton().setLoggingLevel(CEGUI::Informative);
        }
    }
}

bool GUI::init(PlatformContext& context, ResourceCache* cache) {
    if (_init) {
        Console::d_errorfn(Locale::Get(_ID("ERROR_GUI_DOUBLE_INIT")));
        return false;
    }

    _ceguiInput.init(parent().platformContext().config());

    _console = MemoryManager_NEW GUIConsole(*this, context, cache);
    assert(_console);

    GUIButton::soundCallback([&context](const AudioDescriptor_ptr& sound) { context.sfx().playSound(sound); });

    const Configuration::GUI& guiConfig = parent().platformContext().config().gui;
    if (guiConfig.cegui.enabled) {

        const vec2<U16>& renderSize = context.gfx().renderingResolution();
        const CEGUI::Sizef size(static_cast<float>(renderSize.width), static_cast<float>(renderSize.height));

        CEGUI::DefaultResourceProvider* rp = static_cast<CEGUI::DefaultResourceProvider*>(CEGUI::System::getSingleton().getResourceProvider());

        const CEGUI::String CEGUIInstallSharePath((Paths::g_assetsLocation + Paths::g_GUILocation).c_str());
        rp->setResourceGroupDirectory("schemes", CEGUIInstallSharePath + "schemes/");
        rp->setResourceGroupDirectory("imagesets", CEGUIInstallSharePath + "imagesets/");
        rp->setResourceGroupDirectory("fonts", CEGUIInstallSharePath + Paths::g_fontsPath.c_str());
        rp->setResourceGroupDirectory("layouts", CEGUIInstallSharePath + "layouts/");
        rp->setResourceGroupDirectory("looknfeels", CEGUIInstallSharePath + "looknfeel/");
        rp->setResourceGroupDirectory("lua_scripts", CEGUIInstallSharePath + "lua_scripts/");
        rp->setResourceGroupDirectory("schemas", CEGUIInstallSharePath + "xml_schemas/");
        rp->setResourceGroupDirectory("animations", CEGUIInstallSharePath + "animations/");

        // set the default resource groups to be used
        CEGUI::ImageManager::setImagesetDefaultResourceGroup("imagesets");
        CEGUI::Font::setDefaultResourceGroup("fonts");
        CEGUI::Scheme::setDefaultResourceGroup("schemes");
        CEGUI::WidgetLookManager::setDefaultResourceGroup("looknfeels");
        CEGUI::WindowManager::setDefaultResourceGroup("layouts");
        CEGUI::ScriptModule::setDefaultResourceGroup("lua_scripts");
        // setup default group for validation schemas
        CEGUI::XMLParser* parser = CEGUI::System::getSingleton().getXMLParser();
        if (parser->isPropertyPresent("SchemaDefaultResourceGroup")) {
            parser->setProperty("SchemaDefaultResourceGroup", "schemas");
        }
        CEGUI::FontManager::getSingleton().createFromFile("DejaVuSans-10.font");
        CEGUI::FontManager::getSingleton().createFromFile("DejaVuSans-12.font");
        CEGUI::FontManager::getSingleton().createFromFile("DejaVuSans-10-NoScale.font");
        CEGUI::FontManager::getSingleton().createFromFile("DejaVuSans-12-NoScale.font");
        CEGUI::SchemeManager::getSingleton().createFromFile((defaultGUIScheme() + ".scheme").c_str());

        // We create a CEGUI texture target and create a GUIContext that will use it.

        _ceguiRenderer = CEGUI::System::getSingleton().getRenderer();
        _ceguiRenderTextureTarget = _ceguiRenderer->createTextureTarget();
        _ceguiRenderTextureTarget->declareRenderSize(size);
        _ceguiContext = &CEGUI::System::getSingleton().createGUIContext(static_cast<CEGUI::RenderTarget&>(*_ceguiRenderTextureTarget));

        _rootSheet = CEGUI::WindowManager::getSingleton().createWindow("DefaultWindow", "root_window");
        _rootSheet->setMousePassThroughEnabled(true);
        _rootSheet->setUsingAutoRenderingSurface(false);
        _rootSheet->setPixelAligned(false);

        _ceguiContext->setRootWindow(_rootSheet);
        _ceguiContext->setDefaultTooltipType((defaultGUIScheme() + "/Tooltip").c_str());

        _console->createCEGUIWindow();
        CEGUI::System::getSingleton().notifyDisplaySizeChanged(size);
    }

    PipelineDescriptor pipelineDesc = {};
    pipelineDesc._stateHash = context.gfx().getDefaultStateBlock(false);
    pipelineDesc._shaderProgramHandle = context.gfx().defaultIMShader()->handle();
    pipelineDesc._primitiveTopology = PrimitiveTopology::TRIANGLES;

    _postCEGUIPipeline = context.gfx().newPipeline(pipelineDesc);

    recreateDefaultMessageBox();

    _init = true;
    return true;
}

void GUI::recreateDefaultMessageBox() {
    g_assertMsgBox = nullptr;
    if (_defaultMsgBox != nullptr) {
        MemoryManager::DELETE(_defaultMsgBox);
    }

    const Configuration::GUI& guiConfig = parent().platformContext().config().gui;
    if (guiConfig.cegui.enabled) {
        _defaultMsgBox = MemoryManager_NEW GUIMessageBox("AssertMsgBox",
                                                         "Assertion failure",
                                                         "Assertion failed with message: ",
                                                         vec2<I32>(0),
                                                         _context->rootSheet());
    }
    g_assertMsgBox = _defaultMsgBox;
}

void GUI::destroy() {
    if (_init) {
        Console::printfn(Locale::Get(_ID("STOP_GUI")));
        MemoryManager::SAFE_DELETE(_console);
        MemoryManager::SAFE_DELETE(_defaultMsgBox);
        g_assertMsgBox = nullptr;

        {
            ScopedLock<SharedMutex> w_lock(_guiStackLock);
            assert(_guiStack.empty());
            for (U8 i = 0; i < to_base(GUIType::COUNT); ++i) {
                for (auto& [nameHash, entry] : _guiElements[i]) {
                    MemoryManager::DELETE(entry.first);
                }
                _guiElements[i].clear();
            }
        }

        const Configuration::GUI& guiConfig = parent().platformContext().config().gui;
        if (guiConfig.cegui.enabled) {
            // Close CEGUI
            try {
                CEGUI::System::destroy();
            } catch (...) {
                Console::d_errorfn(Locale::Get(_ID("ERROR_CEGUI_DESTROY")));
            }
        }
        _init = false;
    }
}

void GUI::showDebugCursor(const bool state) {
    _showDebugCursor = state;

    if (_rootSheet != nullptr) {
        _rootSheet->setMouseCursor(state ? "GWEN/Tree.Plus" : "");
    }
}

void GUI::onResolutionChange(const SizeChangeParams& params) {
    if (!params.isMainWindow) {
        return;
    }

    if (parent().platformContext().config().gui.cegui.enabled) {
        const CEGUI::Sizef windowSize(params.width, params.height);
        CEGUI::System::getSingleton().notifyDisplaySizeChanged(windowSize);
        if (_ceguiRenderTextureTarget) {
            _ceguiRenderTextureTarget->declareRenderSize(windowSize);
        }


        if (_rootSheet) {
            const Rect<I32>& renderViewport = { 0, 0, params.width, params.height };
            _rootSheet->setSize(CEGUI::USize(CEGUI::UDim(0.0f, to_F32(renderViewport.z)),
                CEGUI::UDim(0.0f, to_F32(renderViewport.w))));
            _rootSheet->setPosition(CEGUI::UVector2(CEGUI::UDim(0.0f, to_F32(renderViewport.x)),
                CEGUI::UDim(0.0f, to_F32(renderViewport.y))));
        }
    }
}

void GUI::setCursorPosition(const I32 x, const I32 y) {
    const Configuration::GUI& guiConfig = parent().platformContext().config().gui;
    if (guiConfig.cegui.enabled) {
        getCEGUIContext()->injectMousePosition(to_F32(x), to_F32(y));
    }
}

// Return true if input was consumed
bool GUI::onKeyDown(const Input::KeyEvent& key) {
    return _ceguiInput.onKeyDown(key);
}

// Return true if input was consumed
bool GUI::onKeyUp(const Input::KeyEvent& key) {
    return _ceguiInput.onKeyUp(key);
}

// Return true if input was consumed
bool GUI::mouseMoved(const Input::MouseMoveEvent& arg) {
    return _ceguiInput.mouseMoved(arg);
}

// Return true if input was consumed
bool GUI::mouseButtonPressed(const Input::MouseButtonEvent& arg) {
    return _ceguiInput.mouseButtonPressed(arg);
}

// Return true if input was consumed
bool GUI::mouseButtonReleased(const Input::MouseButtonEvent& arg) {
    return _ceguiInput.mouseButtonReleased(arg);
}

// Return true if input was consumed
bool GUI::joystickAxisMoved(const Input::JoystickEvent& arg) {
    return _ceguiInput.joystickAxisMoved(arg);
}

// Return true if input was consumed
bool GUI::joystickPovMoved(const Input::JoystickEvent& arg) {
    return _ceguiInput.joystickPovMoved(arg);
}

// Return true if input was consumed
bool GUI::joystickButtonPressed(const Input::JoystickEvent& arg) {
    return _ceguiInput.joystickButtonPressed(arg);
}

// Return true if input was consumed
bool GUI::joystickButtonReleased(const Input::JoystickEvent& arg) {
    return _ceguiInput.joystickButtonReleased(arg);
}

// Return true if input was consumed
bool GUI::joystickBallMoved(const Input::JoystickEvent& arg) {
    return _ceguiInput.joystickBallMoved(arg);
}

// Return true if input was consumed
bool GUI::joystickAddRemove(const Input::JoystickEvent& arg) {
    return _ceguiInput.joystickAddRemove(arg);
}

bool GUI::joystickRemap(const Input::JoystickEvent &arg) {
    return _ceguiInput.joystickRemap(arg);
}

bool GUI::onUTF8([[maybe_unused]] const Input::UTF8Event& arg) noexcept {
    return false;
}

GUIElement* GUI::getGUIElementImpl(const I64 sceneID, const U64 elementName, const GUIType type) const {
    if (sceneID != 0) {
        SharedLock<SharedMutex> r_lock(_guiStackLock);
        const GUIMapPerScene::const_iterator it = _guiStack.find(sceneID);
        if (it != std::cend(_guiStack)) {
            return it->second->getGUIElement<GUIElement>(elementName);
        }
    } else {
        return GUIInterface::getGUIElementImpl(elementName, type);
    }

    return nullptr;
}

GUIElement* GUI::getGUIElementImpl(const I64 sceneID, const I64 elementID, const GUIType type) const {
    if (sceneID != 0) {
        SharedLock<SharedMutex> r_lock(_guiStackLock);
        const GUIMapPerScene::const_iterator it = _guiStack.find(sceneID);
        if (it != std::cend(_guiStack)) {
            return it->second->getGUIElement<GUIElement>(elementID);
        }
    } else {
        return GUIInterface::getGUIElementImpl(elementID, type);
    }

    return nullptr;
}
CEGUI::GUIContext* GUI::getCEGUIContext() noexcept {
    return _ceguiContext;
}

TextureData GUI::getCEGUIRenderTextureData() const {
    if (_ceguiRenderTextureTarget != nullptr) {
        const GFXDevice& gfx = _context->parent().platformContext().gfx();
        return
        {
            gfx.getHandleFromCEGUITexture(_ceguiRenderTextureTarget->getTexture()),
            TextureType::TEXTURE_2D 
        };

    }

    return { 0u, TextureType::COUNT };
}
};
