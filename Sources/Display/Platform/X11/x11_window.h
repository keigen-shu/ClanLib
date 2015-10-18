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
		const DisplayWindowHandle &get_handle() const { return handle; }

		float get_ppi() const { return ppi; }
		float get_pixel_ratio() const { return pixel_ratio; }

		Rect get_geometry() const { throw Exception("EWMH Frame extents not yet implemented."); }
		Rect get_viewport() const { return { 0, 0, client_window_size }; }

		//! Always returns minimum size for client area.
		Size get_minimum_size() const { return minimum_size; }
		//! Always returns maximum size for client area.
		Size get_maximum_size() const { return maximum_size; }

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
		//! Sets the pixel ratio of the window.
		void set_pixel_ratio(float new_ratio);

		//! Moves the client window to a new position.
		//!
		//! The position supplied should be interpreted as the top-left point of
		//! the window frame, not the actual drawable client area. Some ICCCM
		//! non-compliant WMs may still move the window to a different position.
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

		//! Changes the maximum size at which the client window can be resized.
		//!
		//! The size always refers to the client drawable area and does not
		//! include the lengths of the window frames.
		void set_maximum_size(const Size &new_size);


		//! Sets the title of the window.
		void set_title(const std::string &new_title);

		//! Sets whether or not the window should accept input device events.
		void set_enabled(bool new_state);

		//! Sets the fullscreen state of the window. Pass in `true` to make
		//! window go into fullscreen and `false` to go out of fullscreen.
		void set_fullscreen(bool new_state);

		void minimize();
		void maximize();
		void restore();

		void bring_to_front();

		void show(bool activate);
		void hide();

		void request_repaint();

		void set_clipboard_text(const std::string &text) { cb_text = text; /* TODO: NotifyClipboard? */ } 
		void set_clipboard_image(const PixelBuffer &pixel_buffer) { throw Exception("Setting image to clipboard is not yet implemented."); }

		//! Request X to use a normal pointer cursor.
		void show_system_cursor();

		//! Request X to use an invisible pointer cursor (hence "hiding" it).
		void hide_system_cursor();

		//! Request X to use a particular preset cursor type.
		void set_cursor(StandardCursor type);

		//! Redirects mouse imput into or away from this window.
		void capture_mouse(bool new_state);

	public:
		//! Retrieves the top-left screen coordinate and size of the client
		//! window (meaning it excludes window decorations).
		Rect get_screen_position() const;

		Point client_to_screen(const Point &client) const;
		Point screen_to_client(const Point &screen) const;

		//! Processes an XEvent on this window object.
		//! \warn This function asserts that XEvent::window contains the same
		//!       IS as this window _for certain event types_.
		void process_event(XEvent &event, X11Window *mouse_capture_window);

		//! Update the window. This should be called when all events have
		//! been received.
		void process_window();

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

		//! Sends a _NET_REQUEST_FRAME_EXTENTS event and checks if WM sends back
		//! the appropriate PropertyNotify event.
		//! \see EWMH's _NET_REQUEST_FRAME_EXTENTS.
		bool request_frame_extents() const;

		//! Updates `frame_extents` with the value in _NET_FRAME_EXTENTS property.
		void refresh_frame_extents();

		//! Predicate functor for XCheckIfEvent call in `request_frame_extents`.
		static Bool xCheckIfEventPredicate_RequestFrameExtents(::Display*, XEvent*, XPointer);

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

		bool is_exposed; //!< This is set when Expose event is received and reset when the screen is repainted.

		//! Signifies that the window has been minimized by the WM.
		//! This is set to true when an UnmapNotify event is received and reset
		//! to false when MapNotify event is received
		bool external_minimize;

		//! If set to true, last_position will not be modified until a MapNotify
		//! event is received in process_events. Once received, frame_extents
		//! will be calculated, last_position will be adjusted accordingly and
		//! then this boolean is set to `false`.
		bool compensate_frame_extents_on_MapNotify;

		Rect frame_extents; //!< The lengths of the window frame deco added by WM.

		Point last_position; //!< Position supplied to previous XMoveWindow request or received through XConfigureEvent.
		Size  last_size; //!< Size supplied to previous XResizeWindow request.

		XConfigureEvent last_XCE; //!< The XConfigureEvent received on the previous call to process_message.

		Size minimum_size; //!< Minimum size for client area. Read-only cache value.
		Size maximum_size; //!< Maximum size for client area. Read-only cache value.

		//! Client window top-left position relative to screen (excludes
		//! WM-added window decorations).
		Point client_window_position;
		Size  client_window_size;

		std::string window_title; //!< Window title. Read-only cache value.

		::Cursor system_cursor;    //!< System cursor handle.
		::Cursor invisible_cursor; //!< Invisible cursor handle.
		Pixmap   invisible_pixmap; //!< Invisible cursor pixmap. TODO Make me global static.

		InputDevice keyboard;
		InputDevice mouse;
		std::vector<InputDevice> joysticks;

		std::function<bool(XButtonEvent&)> fn_on_click;
		std::function<void()> fn_on_resize;

		// X11Clipboard
		std::string cb_text;
		PixelBuffer cb_image;
		
		float ppi = 96.0f;
		float pixel_ratio = 0.0f; //!< Window dip to ppx ratio. 0.0f = Unset

	public: // Do not move me. Both GCC and Clang need `fn_on_click` above me to work.
		auto func_on_click () -> decltype(fn_on_click ) & { return fn_on_click; }
		auto func_on_resize() -> decltype(fn_on_resize) & { return fn_on_resize; }
	};
}
