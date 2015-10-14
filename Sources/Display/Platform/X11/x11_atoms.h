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
**    Chu Chin Kuan
*/
#include <map>
#include <string>
#include <vector>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "API/Core/Text/logger.h"

namespace clan
{
	//! X11 Atom handler.
	class X11Atoms
	{
	public:
		//! String to Atom map container.
		using AtomMap = std::map< std::string, Atom >;

		//! Static list of all atoms used by ClanLib.
		static const std::vector< std::string > _atoms_;

	public:
		//! Empty X11Atoms initializer.
		X11Atoms() : _display_(nullptr), _screen_(-1) { }
		X11Atoms(::Display *display, int screen) : _display_(display), _screen_(screen)
		{
			clear();
			populate();
		}

		Atom       &operator[](const std::string &elem);
		Atom const &operator[](const std::string &elem) const;

		bool exists(const std::string &elem) const;

		Atom get_atom(::Display *display, const char *elem, bool only_if_exists);

		//! Loads Atoms from the X display.
		void populate();

		//! Clears the AtomMap.
		void clear();

		// Important: Use XFree() on the returned pointer (if not NULL)
		static unsigned char *get_property(::Display *display, Window window, Atom property, Atom &actual_type, int &actual_format, unsigned long &item_count);
		static unsigned char *get_property(::Display *display, Window window, Atom property, unsigned long &item_count);

		inline unsigned char *get_property(Window window, const std::string &property, unsigned long &item_count) const
		{
			return get_property(_display_, window, (*this)[property], item_count);
		}

		//////////////////////////
		// wm-spec related methods
		//////////////////////////
		inline bool is_hint_supported(const std::string &net_atom) const
		{
			// No need to check for _NET_SUPPORTED, since _net_ would be empty.
			return _net_.find(net_atom) != _net_.cend();
		}

		//! Tests if atoms listed in `state_atoms` exist in _NET_WM_STATE.
		//! \returns An empty vector on failure: if _NET_WM_STATE Atom does not
		//!          exist; or if XGetWindowProperty failed. Otherwise, it will
		//!          return a vector with the same number of elements as
		//!          `state_atoms`.
		std::vector<bool> check_net_wm_state(Window window, const std::vector<std::string> &state_atoms) const;

		//! \returns false on failure.
		bool modify_net_wm_state(Window window, long action, const std::string &atom1, const std::string &atom2 = None);

	private:
		::Display *_display_;
		int        _screen_;
		AtomMap    _map_;
		AtomMap    _net_; // _NET_SUPPORTED;
	};
}
