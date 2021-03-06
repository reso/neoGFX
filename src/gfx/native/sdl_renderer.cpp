// sdl_renderer.cpp
/*
  neogfx C++ GUI Library
  Copyright (c) 2015 Leigh Johnston.  All Rights Reserved.
  
  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <neogfx/neogfx.hpp>
#include <SDL.h>
#include <neolib/scoped.hpp>

#include <neogfx/hid/surface_manager.hpp>
#include <neogfx/gui/window/i_window.hpp>
#include "../../gui/window/native/sdl_window.hpp"
#include "sdl_renderer.hpp"

namespace neogfx
{
    class sdl_instance
    {
    public:
        sdl_instance()
        {
            SDL_Init(SDL_INIT_VIDEO);
            SDL_SetHint(SDL_HINT_WINDOWS_ENABLE_MESSAGELOOP, "0");
        }
        ~sdl_instance()
        {
            SDL_Quit();
        }
    public:
        static void instantiate()
        {
            static sdl_instance sSdlInstance;
        }
    };

    class offscreen_sdl_window : public offscreen_window
    {
    public:
        offscreen_sdl_window() : iHandle{ nullptr }
        {
            iHandle = SDL_CreateWindow(
                "neogfx::offscreen_sdl_window",
                0,
                0,
                0,
                0,
                SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
            if (iHandle == nullptr)
                throw sdl_renderer::failed_to_create_offscreen_window(SDL_GetError());
        }
        ~offscreen_sdl_window()
        {
            SDL_DestroyWindow(iHandle);
        }
    public:
        void* handle() const override
        {
            return iHandle;
        }
    private:
        SDL_Window* iHandle;
    };

    sdl_renderer::sdl_renderer(neogfx::renderer aRenderer, bool aDoubleBufferedWindows) :
        opengl_renderer{ aRenderer },
        iDoubleBuffering{ aDoubleBufferedWindows },
        iCreatingWindow{ 0 },
        iContext{ nullptr },
        iInitialized{ false }
    {
        if (aRenderer != neogfx::renderer::None)
        {
            SDL_AddEventWatch(&filter_event, this);
            sdl_instance::instantiate();
            switch (aRenderer)
            {
            case renderer::Vulkan:
            case renderer::Software:
                throw unsupported_renderer();
            case renderer::DirectX: // ANGLE
#ifdef _WIN32
                SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, aDoubleBufferedWindows ? 1 : 0);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_EGL, 1);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
                throw unsupported_renderer();
#endif
                break;
            case renderer::OpenGL:
                SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, aDoubleBufferedWindows ? 1 : 0);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
                break;
            default:
                break;
            }
        }
    }

    sdl_renderer::~sdl_renderer()
    {
        cleanup();
    }

    void sdl_renderer::initialize()
    {
        if (!iInitialized)
        {
            auto dow = allocate_offscreen_window(nullptr);
            iDefaultOffscreenWindow = dow;
            iContext = create_context(dow->handle());
            glCheck(SDL_GL_MakeCurrent(static_cast<SDL_Window*>(dow->handle()), iContext));
            glCheck(glewInit());
            opengl_renderer::initialize();
            iInitialized = true;
        }
    }

    void sdl_renderer::cleanup()
    {
        if (iInitialized && renderer() != neogfx::renderer::None)
        {
            if (iContext != nullptr)
                glCheck(SDL_GL_MakeCurrent(static_cast<SDL_Window*>(iDefaultOffscreenWindow.lock()->handle()), iContext));
            opengl_renderer::cleanup();
            if (iContext != nullptr)
                destroy_context(iContext);
            iOffscreenWindows.clear();
            iOffscreenWindowPool.clear();
        }
    }

    bool sdl_renderer::double_buffering() const
    {
        return iDoubleBuffering;
    }

    const i_render_target* sdl_renderer::active_target() const
    {
        if (iTargetStack.empty())
            return nullptr;
        return iTargetStack.back();
    }

    void sdl_renderer::activate_context(const i_render_target& aTarget)
    {
        //if constexpr (!ndebug)
        //    std::cerr << "sdl_renderer: activating context..." << std::endl;

        if (iContext == nullptr)
            iContext = create_context(aTarget);

        iTargetStack.push_back(&aTarget);
        
        if (!iInitialized)
            initialize();

        activate_current_target();

        //if constexpr (!ndebug)
        //    std::cerr << "sdl_renderer: context activated" << std::endl;
    }

    void sdl_renderer::deactivate_context()
    {
        //if constexpr (!ndebug)
        //    std::cerr << "sdl_renderer: deactivating context..." << std::endl;

        if (active_target() != nullptr)
            deallocate_offscreen_window(active_target());

        if (iTargetStack.empty())
            throw no_target_active();
        iTargetStack.pop_back();

        auto activeTarget = active_target();
        if (activeTarget != nullptr)
        {
            iTargetStack.pop_back();
            activeTarget->activate_target();
        }
        else
            activate_current_target();

        //if constexpr (!ndebug)
        //    std::cerr << "sdl_renderer: context deactivated" << std::endl;
    }

    sdl_renderer::opengl_context sdl_renderer::create_context(const i_render_target& aTarget)
    {
        return create_context(aTarget.target_type() == render_target_type::Surface ? aTarget.target_handle() : allocate_offscreen_window(&aTarget)->handle());
    }

    void sdl_renderer::destroy_context(opengl_context aContext)
    {
        SDL_GL_DeleteContext(static_cast<SDL_GLContext>(aContext));
        if (iContext == aContext)
            iContext = nullptr;
    }

    std::unique_ptr<i_native_window> sdl_renderer::create_window(i_surface_manager& aSurfaceManager, i_surface_window& aWindow, const video_mode& aVideoMode, const std::string& aWindowTitle, window_style aStyle)
    {
        neolib::scoped_counter<uint32_t> sc(iCreatingWindow);
        return std::unique_ptr<i_native_window>(new sdl_window{ *this, aSurfaceManager, aWindow, aVideoMode, aWindowTitle, aStyle });
    }

    std::unique_ptr<i_native_window> sdl_renderer::create_window(i_surface_manager& aSurfaceManager, i_surface_window& aWindow, const size& aDimensions, const std::string& aWindowTitle, window_style aStyle)
    {
        neolib::scoped_counter<uint32_t> sc(iCreatingWindow);
        return std::unique_ptr<i_native_window>(new sdl_window{ *this, aSurfaceManager, aWindow, aDimensions, aWindowTitle, aStyle });
    }

    std::unique_ptr<i_native_window> sdl_renderer::create_window(i_surface_manager& aSurfaceManager, i_surface_window& aWindow, const point& aPosition, const size& aDimensions, const std::string& aWindowTitle, window_style aStyle)
    {
        neolib::scoped_counter<uint32_t> sc(iCreatingWindow);
        return std::unique_ptr<i_native_window>(new sdl_window{ *this, aSurfaceManager, aWindow, aPosition, aDimensions, aWindowTitle, aStyle });
    }

    std::unique_ptr<i_native_window> sdl_renderer::create_window(i_surface_manager& aSurfaceManager, i_surface_window& aWindow, i_native_surface& aParent, const video_mode& aVideoMode, const std::string& aWindowTitle, window_style aStyle)
    {
        neolib::scoped_counter<uint32_t> sc(iCreatingWindow);
        sdl_window* parent = dynamic_cast<sdl_window*>(&aParent);
        if (parent != nullptr)
            return std::unique_ptr<i_native_window>(new sdl_window{ *this, aSurfaceManager, aWindow, *parent, aVideoMode, aWindowTitle, aStyle });
        else
            return create_window(aSurfaceManager, aWindow, aVideoMode, aWindowTitle, aStyle);
    }

    std::unique_ptr<i_native_window> sdl_renderer::create_window(i_surface_manager& aSurfaceManager, i_surface_window& aWindow, i_native_surface& aParent, const size& aDimensions, const std::string& aWindowTitle, window_style aStyle)
    {
        neolib::scoped_counter<uint32_t> sc(iCreatingWindow);
        sdl_window* parent = dynamic_cast<sdl_window*>(&aParent);
        if (parent != nullptr)
            return std::unique_ptr<i_native_window>(new sdl_window{ *this, aSurfaceManager, aWindow, *parent, aDimensions, aWindowTitle, aStyle });
        else
            return create_window(aSurfaceManager, aWindow, aDimensions, aWindowTitle, aStyle);
    }

    std::unique_ptr<i_native_window> sdl_renderer::create_window(i_surface_manager& aSurfaceManager, i_surface_window& aWindow, i_native_surface& aParent, const point& aPosition, const size& aDimensions, const std::string& aWindowTitle, window_style aStyle)
    {
        neolib::scoped_counter<uint32_t> sc(iCreatingWindow);
        sdl_window* parent = dynamic_cast<sdl_window*>(&aParent);
        if (parent != nullptr)
            return std::unique_ptr<i_native_window>(new sdl_window{ *this, aSurfaceManager, aWindow, *parent, aPosition, aDimensions, aWindowTitle, aStyle });
        else
            return create_window(aSurfaceManager, aWindow, aPosition, aDimensions, aWindowTitle, aStyle);
    }

    bool sdl_renderer::creating_window() const
    {
        return iCreatingWindow != 0;
    }

    void sdl_renderer::render_now()
    {
        service<i_surface_manager>().render_surfaces();
    }

    bool sdl_renderer::use_rendering_priority() const
    {
        // todo
        return false;
    }

    bool sdl_renderer::process_events()
    {
        bool eventsAlreadyQueued = false;
        for (std::size_t s = 0; !eventsAlreadyQueued && s < service<i_surface_manager>().surface_count(); ++s)
        {
            auto& surface = service<i_surface_manager>().surface(s);
            if (!surface.has_native_surface())
                continue;
            if (surface.surface_type() == surface_type::Window && static_cast<i_native_window&>(surface.native_surface()).events_queued())
                eventsAlreadyQueued = true;
        }
        if (queue_events() || eventsAlreadyQueued)
            return opengl_renderer::process_events();
        else
            return false;
    }

    sdl_renderer::handle sdl_renderer::create_context(void* aNativeSurfaceHandle)
    {
        handle result;
        glCheck(result = SDL_GL_CreateContext(static_cast<SDL_Window*>(aNativeSurfaceHandle)));
        return result;
    }

    std::shared_ptr<offscreen_window> sdl_renderer::allocate_offscreen_window(const i_render_target* aRenderTarget)
    {
        auto existingWindow = iOffscreenWindows.find(aRenderTarget);
        if (existingWindow != iOffscreenWindows.end())
            return existingWindow->second;
        for (auto& ow : iOffscreenWindowPool)
        {
            if (ow.use_count() == 1)
            {
                iOffscreenWindows[aRenderTarget] = ow;
                return ow;
            }
        }
        auto newOffscreenWindow = std::make_shared<offscreen_sdl_window>();
        iOffscreenWindowPool.push_back(newOffscreenWindow);
        iOffscreenWindows[aRenderTarget] = newOffscreenWindow;
        return newOffscreenWindow;
    }

    void sdl_renderer::deallocate_offscreen_window(const i_render_target* aRenderTarget)
    {
        auto iterRemove = iOffscreenWindows.find(aRenderTarget);
        if (iterRemove != iOffscreenWindows.end())
            iOffscreenWindows.erase(iterRemove);
    }

    void sdl_renderer::activate_current_target()
    {
        glCheck(
            if (SDL_GL_MakeCurrent(static_cast<SDL_Window*>(active_target() != nullptr && active_target()->target_type() == render_target_type::Surface ? 
                active_target()->target_handle() : allocate_offscreen_window(active_target())->handle()), static_cast<SDL_GLContext>(iContext)) == -1)
                throw failed_to_activate_opengl_context(SDL_GetError());
        )
        glCheck(SDL_GL_SetSwapInterval(0));
    }

    int sdl_renderer::filter_event(void* aSelf, SDL_Event* aEvent)
    {
        switch (aEvent->type)
        {
        case SDL_WINDOWEVENT:
            {
                SDL_Window* sdlWindow = SDL_GetWindowFromID(aEvent->window.windowID);
                if (sdlWindow != NULL && service<i_surface_manager>().is_surface_attached(sdlWindow))
                {
                    auto& window = static_cast<sdl_window&>(service<i_surface_manager>().attached_surface(sdlWindow).native_surface());
                    switch (aEvent->window.event)
                    {
                    case SDL_WINDOWEVENT_ENTER:
                        aEvent->window.data1 = static_cast<Sint32>(window.surface_window().as_window().mouse_position().x);
                        aEvent->window.data2 = static_cast<Sint32>(window.surface_window().as_window().mouse_position().y);
                        break;
                    }
                }
            }
            break;
        default:
            break;
        }
        return 0;
    }

    bool sdl_renderer::queue_events()
    {
        bool queuedEvents = false;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            queuedEvents = true;
            switch (event.type)
            {
            case SDL_WINDOWEVENT:
                {
                    SDL_Window* window = SDL_GetWindowFromID(event.window.windowID);
                    if (window != NULL && service<i_surface_manager>().is_surface_attached(window))
                        static_cast<sdl_window&>(service<i_surface_manager>().attached_surface(window).native_surface()).process_event(event);
                }
                break;
            case SDL_MOUSEMOTION:
                {
                    SDL_Window* window = SDL_GetWindowFromID(event.motion.windowID);
                    if (window != NULL && service<i_surface_manager>().is_surface_attached(window))
                        static_cast<sdl_window&>(service<i_surface_manager>().attached_surface(window).native_surface()).process_event(event);
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                {
                    SDL_Window* window = SDL_GetWindowFromID(event.button.windowID);
                    if (window != NULL && service<i_surface_manager>().is_surface_attached(window))
                        static_cast<sdl_window&>(service<i_surface_manager>().attached_surface(window).native_surface()).process_event(event);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                {
                    SDL_Window* window = SDL_GetWindowFromID(event.button.windowID);
                    if (window != NULL && service<i_surface_manager>().is_surface_attached(window))
                        static_cast<sdl_window&>(service<i_surface_manager>().attached_surface(window).native_surface()).process_event(event);
                }
                break;
            case SDL_MOUSEWHEEL:
                {
                    SDL_Window* window = SDL_GetWindowFromID(event.wheel.windowID);
                    if (window != NULL && service<i_surface_manager>().is_surface_attached(window))
                        static_cast<sdl_window&>(service<i_surface_manager>().attached_surface(window).native_surface()).process_event(event);
                }
                break;
            case SDL_KEYDOWN:
                {
                    SDL_Window* window = SDL_GetWindowFromID(event.key.windowID);
                    if (window != NULL && service<i_surface_manager>().is_surface_attached(window))
                        static_cast<sdl_window&>(service<i_surface_manager>().attached_surface(window).native_surface()).process_event(event);
                }
                break;
            case SDL_KEYUP:
                {
                    SDL_Window* window = SDL_GetWindowFromID(event.key.windowID);
                    if (window != NULL && service<i_surface_manager>().is_surface_attached(window))
                        static_cast<sdl_window&>(service<i_surface_manager>().attached_surface(window).native_surface()).process_event(event);
                }
                break;
            case SDL_TEXTEDITING:
                {
                    SDL_Window* window = SDL_GetWindowFromID(event.edit.windowID);
                    if (window != NULL && service<i_surface_manager>().is_surface_attached(window))
                        static_cast<sdl_window&>(service<i_surface_manager>().attached_surface(window).native_surface()).process_event(event);
                }
                break;
            case SDL_TEXTINPUT:
                {
                    SDL_Window* window = SDL_GetWindowFromID(event.text.windowID);
                    if (window != NULL && service<i_surface_manager>().is_surface_attached(window))
                        static_cast<sdl_window&>(service<i_surface_manager>().attached_surface(window).native_surface()).process_event(event);
                }
                break;
            default:
                break;
            }
        }
        return queuedEvents;
    }
}