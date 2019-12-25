// fragment_shader.hpp
/*
  neogfx C++ GUI Library
  Copyright (c) 2019 Leigh Johnston.  All Rights Reserved.
  
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

#include <neogfx/neogfx.hpp>
#include <neogfx/gfx/primitives.hpp>
#include <neogfx/gfx/i_rendering_context.hpp>
#include <neogfx/gfx/i_texture.hpp>
#include <neogfx/gfx/shader_array.hpp>
#include <neogfx/gfx/i_graphics_context.hpp>
#include <neogfx/gfx/i_shader_program.hpp>
#include <neogfx/gfx/shader.hpp>
#include <neogfx/gfx/i_fragment_shader.hpp>

namespace neogfx
{
    template <typename Base = i_fragment_shader>
    class fragment_shader : public shader<Base>
    {
        typedef shader<Base> base_type;
    public:
        fragment_shader(const std::string& aName) :
            base_type{ shader_type::Fragment, aName }
        {
        }
    };

    template <typename Base = i_fragment_shader>
    class standard_fragment_shader : public fragment_shader<Base>
    {
    public:
        standard_fragment_shader(const std::string& aName = "standard_fragment_shader") :
            fragment_shader{ aName }
        {
            add_in_variable<vec3f>("Coord"_s, 0u);
            auto& fragColor = add_in_variable<vec4f>("Color"_s, 1u);
            add_out_variable<vec4f>("FragColor"_s, 0u).link(fragColor);
        }
    public:
        void generate_code(const i_shader_program& aProgram, shader_language aLanguage, i_string& aOutput) const override
        {
            fragment_shader<Base>::generate_code(aProgram, aLanguage, aOutput);
            if (aProgram.is_first_in_stage(*this))
            {
                if (aLanguage == shader_language::Glsl)
                {
                    static const string code =
                    {
                        "void standard_fragment_shader(inout vec4 color)\n"
                        "{\n"
                        "}\n"_s
                    };
                    aOutput += code;
                }
                else
                    throw unsupported_shader_language();
            }
        }
    };

    constexpr uint32_t GRADIENT_FILTER_SIZE = 15;
    struct gradient_shader_data
    {
        //todo: use a mini atlas for the this
        uint32_t stopCount;
        shader_array<float> stops = { size_u32{gradient::MaxStops, 1} };
        shader_array<std::array<float, 4>> stopColours = { size_u32{gradient::MaxStops, 1} };
        shader_array<float> filter = { size_u32{GRADIENT_FILTER_SIZE, GRADIENT_FILTER_SIZE} };
    };

    class standard_gradient_shader : public standard_fragment_shader<i_gradient_shader>
    {
    private:
        typedef std::list<neogfx::gradient_shader_data> gradient_data_cache_t;
        typedef std::map<gradient, gradient_data_cache_t::iterator> gradient_data_cache_map_t;
        typedef std::deque<gradient_data_cache_map_t::iterator> gradient_data_cache_queue_t;
    private:
        static constexpr std::size_t GRADIENT_DATA_CACHE_QUEUE_SIZE = 64;
    public:
        standard_gradient_shader(const std::string& aName = "standard_gradient_shader");
    public:
        void generate_code(const i_shader_program& aProgram, shader_language aLanguage, i_string& aOutput) const override;
    public:
        void clear_gradient() override;
        void set_gradient(i_rendering_context& aContext, const gradient& aGradient, const rect& aBoundingBox) override;
    private:
        neogfx::gradient_shader_data& gradient_shader_data(const gradient& aGradient);
    private:
        std::vector<float> iGradientStopPositions;
        std::vector<std::array<float, 4>> iGradientStopColours;
        gradient_data_cache_t iGradientDataCache;
        gradient_data_cache_map_t iGradientDataCacheMap;
        gradient_data_cache_queue_t iGradientDataCacheQueue;
        std::optional<neogfx::gradient_shader_data> iUncachedGradient;
    };

    class standard_texture_shader : public standard_fragment_shader<i_texture_shader>
    {
    public:
        standard_texture_shader(const std::string& aName = "standard_texture_shader");
    public:
        void generate_code(const i_shader_program& aProgram, shader_language aLanguage, i_string& aOutput) const override;
    public:
        void clear_texture() override;
        void set_texture(const i_texture& aTexture) override;
        void set_effect(shader_effect aEffect) override;
    };
}