/*
**  ClanLib SDK
**  Copyright (c) 1997-2015 The ClanLib Team
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
**  Note: Some of the libraries ClanLib may link to may have additional
**  requirements or restrictions.
**
**  File Author(s):
**
**    Magnus Norddahl
*/

#pragma once

#include "API/UI/Style/style_property_parser.h"

namespace clan
{
	void force_link_style_parser_outline();

	class OutlinePropertyParser : public StylePropertyParser
	{
	public:
		OutlinePropertyParser() : StylePropertyParser({ "outline" }) { }
		void parse(StylePropertySetter *setter, const std::string &name, StyleParser &parser) override;
	};

	class OutlineColorPropertyParser : public StylePropertyParser
	{
	public:
		OutlineColorPropertyParser() : StylePropertyParser({ "outline-color" }) { }
		void parse(StylePropertySetter *setter, const std::string &name, StyleParser &parser) override;
	};

	class OutlineStylePropertyParser : public StylePropertyParser
	{
	public:
		OutlineStylePropertyParser() : StylePropertyParser({ "outline-style" }) { }
		void parse(StylePropertySetter *setter, const std::string &name, StyleParser &parser) override;
	};

	class OutlineWidthPropertyParser : public StylePropertyParser
	{
	public:
		OutlineWidthPropertyParser() : StylePropertyParser({ "outline-width" }) { }
		void parse(StylePropertySetter *setter, const std::string &name, StyleParser &parser) override;
	};
}
