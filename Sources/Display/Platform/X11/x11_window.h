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
**    Harry Storbacka
**    Mark Page
**    Chu Chin Kuan
*/

#pragma once

#include "API/Core/Math/rect.h" // Rect, Size
#include "API/Core/Signals/signal.h"
#include "API/Display/Image/pixel_buffer.h"
#include "API/Display/TargetProviders/display_window_provider.h" // DisplayWindowSite
#include "API/Display/Window/display_window.h" // DisplayWindowHandle
#include "API/Display/Window/display_window_description.h"
#include "API/Display/Window/input_device.h"

// #include <X11/Xlib.h>
// #include <X11/Xutil.h>

#include "x11_atoms.h"

namespace clan
{
	class InputDeviceProvider_X11Keyboard;
	class InputDeviceProvider_X11Mouse;
	class InputDeviceProvider_LinuxJoystick;
	class CursorProvider_X11;
	class DisplayMessageQueue_X11;

/*! Class containing and managing a single X11 window instance.
 *
 *  The purpose of this class is to simply provide an interface allowing the
 *  library to perform operations on X11 windows.
 *
 *  Methods that have a lower-case 'x' in front are X11 function wrappers.
 */
	class X11Window
	{
	public:
		X11Window();
		~X11Window();

		void create(XVisualInfo *visual, DisplayWindowSite *site, const DisplayWindowDescription &desc);
		void destroy();

	public:
		float get_ppi() const { return ppi; }
		float get_pixel_ratio() const { return pixel_ratio; }

		Rect get_geometry() const { throw Exception("EWMH Frame extents not yet implemented."); }
		Rect get_viewport() const { return { 0, 0, client_window_size }; }

		//! Always returns minimum size for client area.
		Size get_minimum_size() const { return minimum_size; }
		//! Always returns maximum size for client area.
		Size get_maximum_size() const { return maximum_size; }

		const DisplayWindowHandle &get_handle() const { return handle; }

		const std::string &get_title() const { return window_title; }

		bool has_focus() const;

		bool is_fullscreen() const;

		bool is_minimized() const;
		bool is_maximized() const;

		bool is_visible() const { return xGetWindowAttributes().map_state != IsViewable; }

		InputDevice &get_keyboard() { return keyboard; }
		InputDevice &get_mouse() { return mouse; }
		std::vector<InputDevice> &get_game_controllers() { return joysticks; }
		
		bool is_clipboard_text_available() const { throw Exception("Not yet implemented."); }
		bool is_clipboard_image_available() const { throw Exception("Not yet implemented."); }

		std::string get_clipboard_text() const { throw Exception("Not yet implemented."); }
		PixelBuffer get_clipboard_image() const { throw Exception("Not yet implemented."); }
	public:
		//! Moves the client window to a new position.
		//!
		//! Depending on the window manager in use, the position supplied may be
		//! interpreted as the top-left point of _either_ the window frame or
		//! the actual drawable client area. Major WMs (i.e. KDE `KWin 5.4.0` &
		//! Xfce `Xfwm 4.12.3`) will do the former, while some newer or lesser
		//! known WMs may do the latter. Until the situation improves, there is
		//! no reliable way for us to accurately set the position of a window
		//! while knowing whether or not they will take into account the area
		//! taken by window frames.
		//!
		//! \warn The window MUST be in mapped state or an exception will be
		//!       thrown. The underlying X function is known to do nothing when
		//!       the window is unmapped.
		void set_position(const Point &new_pos);

		//! Changes the size of the client window.
		//!
		//! The size always refers to the client drawable area and does not
		//! include the lengths of the window frames.
		void set_size(const Size &new_size);

		//! Changes the minimum size at which the client window can be resized.
		//!
		//! The size always refers to the client drawable area and does not
		//! include the lengths of the window frames.
		void set_minimum_size(const Size &new_size);

		//! Changes the minimum size at which the client window can be resized.
		//!
		//! The size always refers to the client drawable area and does not
		//! include the lengths of the window frames.
		void set_maximum_size(const Size &new_size);


		//! Sets the pixel ratio of the window.
		void set_pixel_ratio(float new_ratio);

		//! Sets the title of the window.
		void set_title(const std::string &new_title);

		//! Sets the fullscreen state of the window. Pass in `true` to make
		//! window go into fullscreen and `false` to go out of fullscreen.
		void set_fullscreen(bool new_state);

		//! Sets whether or not the window should accept input device events.
		void set_enabled(bool new_state);

		void minimize();
		void maximize();
		void restore();

		void bring_to_front();

		void show(bool activate);
		void hide();

		void request_repaint();

		void set_clipboard_text(const std::string &text) { cb_text = text; /* NotifyClipboard? */ } 
		void set_clipboard_image(const PixelBuffer &pixel_buffer) { throw Exception("Setting image to clipboard is not yet implemented."); }

		void show_system_cursor() { throw Exception("unimplemented"); }
		void hide_system_cursor() { throw Exception("unimplemented"); }

		void set_cursor(StandardCursor type) { throw Exception("unimplemented"); }

		void capture_mouse(bool new_state);

	public:
		//! Retrieves the top-left screen coordinate and size of the client
		//! window (meaning it excludes window decorations).
		Rect get_screen_position() const;

		Point client_to_screen(const Point &client) const;
		Point screen_to_client(const Point &screen) const;

	private:
		//! Function called at the start to X11Window::create() to prepare the
		//! class by populating environmental (root window) parameters.
		//!
		//! \warn `this->handle.display` must be set to the X display.
		void prepare();

		//! Changes the window to mapped state.
		//! \throws Exception if window is not in unmapped state.
		void map_window();

		//! Changes the window to unmapped state.
		//! \throws Exception if window is in unmapped state.
		void unmap_window();

		//! Updates client_window_position and client_window_size.
		void refresh_client_window_attributes();

	private: // Xlib function wrappers.
		XWindowAttributes xGetWindowAttributes() const;

		Size xGetScreenSize_px();
		Size xGetScreenSize_mm();

		//! Updates this->size_hints with XGetWMNormalHints from XServer
		void xGetWMNormalHints();

		//! Calls XSetWMNormalHints with this->size_hints.
		//! \note Calling Xlib's XSetWMNormalHints PropertyNotify event to be sent.
		void xSetWMNormalHints();

		//! Copies a collection of Strings into a new XTextProperty object.
		//! \warn Use XFree on returned XTextProperty::value pointer to avoid
		//!       memory leaks.
		static XTextProperty xStringListToTextProperty(const std::vector<std::string> &string_list);

	private:
		DisplayWindowHandle handle;
		DisplayWindowSite * site;

		X11Atoms atoms;

		Colormap colormap;

		XSizeHints * size_hints;

		Point last_position; //!< Position supplied to previous XMoveWindow request.
		Size  last_size; //!< Size supplied to previous XResizeWindow request.

		Size minimum_size; //!< Minimum size for client area. Read-only cache value.
		Size maximum_size; //!< Maximum size for client area. Read-only cache value.

		//! Client window top-left position relative to screen (excludes
		//! WM-added window decorations).
		Point client_window_position;
		Size  client_window_size;

		std::string window_title; //!< Window title. Read-only cache value.

		InputDevice keyboard;
		InputDevice mouse;
		std::vector<InputDevice> joysticks;

		// X11Clipboard
		std::string cb_text;
		// PixelBuffer cb_image;
		

		float ppi = 96.0f;
		float pixel_ratio = 0.0f; //!< Window dip to ppx ratio. 0.0f = Unset
	};
}
