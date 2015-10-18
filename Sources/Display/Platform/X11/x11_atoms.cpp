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

#include "x11_atoms.h"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace clan
{
	const std::vector< std::string > X11Atoms::_atoms_ =
	{
		"WM_PROTOCOLS",

		"WM_CLIENT_MACHINE",
		"WM_DELETE_WINDOW",
		"WM_STATE",

		"CLIPBOARD",
		"PRIMARY",

		"_NET_SUPPORTED",
		"_NET_SUPPORTING_WM_CHECK",

		//! Used to obtain the lengths added by the WM to each side of a window
		//! for window decorations.
		"_NET_FRAME_EXTENTS",

		//! Used to request that the WM calculate the frame extents of a window
		//! at its current configuration. Some WMs do not support this atom but
		//! set _NET_FRAME_EXTENTS even when the window is not mapped.
		"_NET_REQUEST_FRAME_EXTENTS",

		"_NET_WM_FULL_PLACEMENT",

		"_NET_WM_FULLSCREEN_MONITORS",

		"_NET_WM_NAME",

		"_NET_WM_PID",
		"_NET_WM_PING",

		"_NET_WM_STATE", //! Set by WM, lists the following atoms:
		"_NET_WM_STATE_HIDDEN",
		"_NET_WM_STATE_FULLSCREEN",
		"_NET_WM_STATE_MAXIMIZED_HORZ",
		"_NET_WM_STATE_MAXIMIZED_VERT",
		"_NET_WM_STATE_MODAL",

		"_NET_WM_WINDOW_TYPE",

		"_NET_WM_WINDOW_TYPE_DESKTOP",
		"_NET_WM_WINDOW_TYPE_DOCK",
		"_NET_WM_WINDOW_TYPE_TOOLBAR",
		"_NET_WM_WINDOW_TYPE_MENU",
		"_NET_WM_WINDOW_TYPE_UTILITY",
		"_NET_WM_WINDOW_TYPE_SPLASH",
		"_NET_WM_WINDOW_TYPE_DIALOG",
		"_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
		"_NET_WM_WINDOW_TYPE_POPUP_MENU",
		"_NET_WM_WINDOW_TYPE_TOOLTIP",
		"_NET_WM_WINDOW_TYPE_NOTIFICATION",
		"_NET_WM_WINDOW_TYPE_COMBO",
		"_NET_WM_WINDOW_TYPE_DND",
		"_NET_WM_WINDOW_TYPE_NORMAL"
	};

	Atom &X11Atoms::operator[](const std::string &elem)
	{
		auto iter = _map_.find(elem);
		assert(iter != _map_.end()); // Ensure its existence has been checked before.
		return iter->second;
	}

	Atom const &X11Atoms::operator[](const std::string &elem) const
	{
		auto iter = _map_.find(elem);
		assert(iter != _map_.end()); // Ensure its existence has been checked before.
		return iter->second;
	}
	

	bool X11Atoms::exists(const std::string &elem) const
	{
		auto iter = _map_.find(elem);
		if (iter != _map_.end())
			return iter->second != None;
		else
			return false;
	}


	Atom X11Atoms::get_atom(::Display *display, const char *elem, bool only_if_exists)
	{
		assert(display == _display_); // Ensure we're not polluting this data structure.

		Atom atom = XInternAtom(_display_, elem, only_if_exists);
		_map_[std::string(elem)] = atom;
		return atom;
	}


	void X11Atoms::populate()
	{
		log_event("debug", "Populating X11 Display Atoms...");
		for (const auto &elem : _atoms_)
		{
			_map_[elem] = XInternAtom(_display_, elem.c_str(), True);
			log_event("debug", "  %1\t: %2 %3", elem, _map_[elem], (_map_[elem] == None) ? "None" : "OK");
		}

		// Get _NET_SUPPORTED and check for every atom.
		Atom _NET_SUPPORTED = (*this)["_NET_SUPPORTED"];
		if (_NET_SUPPORTED == None)
		{
			log_event("debug", "_NET_SUPPORTED is not provided by WM.");
			return;
		}

		log_event("debug", "Enumerating _NET_SUPPORTED Atoms...");
		unsigned long  item_count = 0;
		unsigned char *data = get_property(_display_, RootWindow(_display_, _screen_), _NET_SUPPORTED, item_count);
		if (data == NULL)
		{
			log_event("debug", "Failed to query _NET_SUPPORTED.");
			return;
		}

		Atom *items_begin = reinterpret_cast<unsigned long*>(data);
		Atom *items_end   = items_begin + item_count;

		for (const auto &elem : _map_)
		{
			if (std::find(items_begin, items_end, elem.second) != items_end)
			{
				_net_[elem.first] = elem.second;
				log_event("debug", "  %1", elem.first);
			}
		}

		log_event("debug", "  ... and %1 others that we don't use.", item_count - _net_.size());

		XFree(data);
	}

	void X11Atoms::clear()
	{
		_net_.clear();
		_map_.clear();
	}

	std::string X11Atoms::get_name(const Atom &atom)
	{
		std::string name;
		char *data = XGetAtomName(_display_, atom);
		if (data != NULL)
		{
			name = data;
			XFree(data);
		}
		return name;
	}

	unsigned char *X11Atoms::get_property(::Display *display, Window window, Atom property, Atom &actual_type, int &actual_format, unsigned long &item_count)
	{
		/* IO */ long  read_bytes = 0; // Request 0 bytes first.
		unsigned long _item_count = item_count;
		unsigned long  bytes_remaining;
		unsigned char *read_data = NULL;

		do
		{
			int result = XGetWindowProperty(
				display, window, property, 0ul, read_bytes,
				False, AnyPropertyType, &actual_type, &actual_format,
				&_item_count, &bytes_remaining, &read_data
			);

			if (result != Success)
			{
				actual_type = None;
				actual_format = 0;
				item_count = 0;
				return NULL;
			}

			read_bytes = bytes_remaining;
		} while (bytes_remaining > 0);

		item_count = _item_count;
		return read_data;
	}

	unsigned char *X11Atoms::get_property(::Display *display, Window window, Atom property, unsigned long &item_count)
	{
		Atom _actual_type;
		int  _actual_format;

		return X11Atoms::get_property(display, window, property, _actual_type, _actual_format, item_count);
	}

	std::vector<bool> X11Atoms::check_net_wm_state(Window window, const std::vector<std::string> &state_atoms) const
	{
		Atom _NET_WM_STATE = (*this)["_NET_WM_STATE"];
		if (_NET_WM_STATE == None)
		{
			log_event("debug", "clan::X11Window::check_net_wm_state() failed: _NET_WM_STATE not provided by WM.");
			return {};
		}

		// Get window states from WM
		unsigned long  item_count = 0;
		unsigned char *data = get_property(_display_, window, _NET_WM_STATE, item_count);
		if (data == NULL)
		{
			log_event("debug", "clan::X11Atoms::check_net_wm_state() failed: Failed to query _NET_WM_STATE.");
			return {};
		}

		Atom *items_begin = reinterpret_cast<unsigned long*>(data);
		Atom *items_end   = items_begin + item_count;

		// Atom not in _NET_WM_STATE MUST be considered not set.
		std::vector< bool > states(state_atoms.size(), false);

		// Map each state atom to state boolean.
		for (size_t i = 0; i < state_atoms.size(); i++)
		{
			const std::string &elem = state_atoms[i];
			Atom state = (*this)[elem];
			if (state == None)
			{
				log_event("debug", "clan::X11Atoms::check_net_wm_state(): %1 is not provided by WM.", elem);
				continue; // Unsupported states are not queried.
			}

			states[i] = std::find(items_begin, items_end, state) != items_end;
		}

		XFree(data);
		return states;
	}

	bool X11Atoms::modify_net_wm_state(Window window, long action, const std::string &atom1, const std::string &atom2)
	{
		Atom _NET_WM_STATE = (*this)["_NET_WM_STATE"];
		if (_NET_WM_STATE == None)
		{
			log_event("debug", "clan::X11Window::modify_net_wm_state() failed: _NET_WM_STATE not provided by WM.");
			return false;
		}

		XEvent xevent;
		memset(&xevent, 0, sizeof(xevent));
		xevent.xclient.type = ClientMessage;
		xevent.xclient.window = window;
		xevent.xclient.message_type = _NET_WM_STATE;
		xevent.xclient.format = 32;
		xevent.xclient.data.l[0] = action;
		xevent.xclient.data.l[1] = (*this)[atom1];
		xevent.xclient.data.l[2] = (*this)[atom2];
		xevent.xclient.data.l[3] = 0; // or 2

		Status ret = XSendEvent(
				_display_, DefaultRootWindow(_display_), False,
				SubstructureNotifyMask | SubstructureRedirectMask, &xevent
				);

		XFlush(_display_);

		if (ret == 0)
		{
			log_event("debug", "clan::X11Atoms::modify_net_wm_state(): XSendEvent failed.");
			return false;
		}
		else
		{
			return true;
		}
	}
}
