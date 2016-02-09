// opengl_window.hpp
/*
  neogfx C++ GUI Library
  Copyright(C) 2016 Leigh Johnston
  
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

#pragma once

#include "neogfx.hpp"
#include <neolib/string_utils.hpp>
#include <boost/lexical_cast.hpp>
#include "neogfx.hpp"
#include <GL/glew.h>
#include <GL/GL.h>
#include <neolib/timer.hpp>
#include "opengl_error.hpp"
#include "native_window.hpp"
#include "i_native_window_event_handler.hpp"
#include "i_native_graphics_context.hpp"

namespace neogfx
{
	class opengl_window : public native_window
	{
	public:
		struct failed_to_create_framebuffer : std::runtime_error { 
			failed_to_create_framebuffer(GLenum aErrorCode) : 
				std::runtime_error("neogfx::opengl_window::failed_to_create_framebuffer: Failed to create frame buffer, reason: " + glErrorString(aErrorCode)) {} };
	public:
		opengl_window(i_rendering_engine& aRenderingEngine, i_surface_manager& aSurfaceManager, i_native_window_event_handler& aEventHandler);
		~opengl_window();
	public:
		virtual bool using_frame_buffer() const;
		virtual void limit_frame_rate(uint32_t aFps);
		virtual void clear_rendering_flag();
	public:
		virtual void invalidate_surface(const rect& aInvalidatedRect);
	public:
		virtual size extents() const;
		virtual dimension horizontal_dpi() const;
		virtual dimension vertical_dpi() const;
		virtual dimension em_size() const;
		void render();
		bool rendered() const;
	protected:
		i_native_window_event_handler& event_handler() const;
	private:
		virtual void activate_context() = 0;
		virtual void deactivate_context() = 0;
		virtual void display() = 0;
		virtual bool processing_event() const = 0;
	private:
		i_native_window_event_handler& iEventHandler;
		size iPixelDensityDpi;
		GLuint iFrameBuffer;
		GLuint iFrameBufferTexture;
		GLuint iDepthStencilBuffer;
		size iFrameBufferSize;
		std::vector<rect> iInvalidatedRects;
		neolib::callback_timer iRenderer;
		boost::optional<uint32_t> iFrameRate;
		uint64_t iLastFrameTime;
		bool iRendered;
		bool iRendering;
	};
}