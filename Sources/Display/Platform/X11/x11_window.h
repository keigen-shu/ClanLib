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
#include "API/Display/TargetProviders/display_window_provider.h" // DisplayWindowSite
#include "API/Display/Window/display_window.h" // DisplayWindowHandle
#include "API/Display/Window/display_window_description.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace clan
{
	class InputDeviceProvider_X11Keyboard;
	class InputDeviceProvider_X11Mouse;
	class InputDeviceProvider_LinuxJoystick;
	class CursorProvider_X11;
	class DisplayMessageQueue_X11;

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

		//! Always returns minimum size for client area
		Size get_minimum_size() const { return minimum_size; }
		//! Always returns maximum size for client area
		Size get_maximum_size() const { return maximum_size; }

		std::string get_title() const { return window_title; }

		const DisplayWindowHandle &get_handle() const { return handle; }

	public:
		/*! Moves the client window to a new position.
		 *
		 *  Depending on the window manager in use, the position supplied may be
		 *  interpreted as the top-left point of either the window frame or the
		 *  actual drawable client area. Major WMs (i.e. KDE `KWin 5.4.0` and
		 *  Xfce `Xfwm 4.12.3`). will do the former, while some lesser known or
		 *  newer WMs may do the latter. Until the situation improves, there is
		 *  no reliable way for us to accurately set the position of a window
		 *  while knowing whether or not they will take into account the area
		 *  taken by window frames.
		 *
		 *  \warn The window MUST be in mapped state or an exception will be
		 *        thrown (the underlying X function is known to not work when
		 *        the window is unmapped).
		 */
		void set_position(const Point &new_pos);

		/*! Changes the size of the client window.
		 *
		 *  The size always refers to the client drawable area and does not
		 *  include the lengths of the window frames.
		 */
		void set_size(const Size &new_size);

		//! Sets the pixel ratio of the window.
		void set_pixel_ratio(float new_ratio);

	private:
		/*! Function called at the start to X11Window::create() to prepare the
		 *  class by populating environmental (root window) parameters.
		 *
		 *  \warn `this->handle.display` must be set to the X display.
		 */
		void prepare();
		void map_window();
		void unmap_window();

	private: // Xlib function wrappers.
		XWindowAttributes xGetWindowAttributes();

		Size xGetScreenSize_px();
		Size xGetScreenSize_mm();

	private:
		DisplayWindowHandle handle;
		DisplayWindowSite * site;

		Colormap colormap;

		XSizeHints * size_hints;

		Point last_position; //! Position supplied on previous XMoveWindow request.
		Size  last_size; //! Size supplied on previous XResizeWindow request.

		Size minimum_size; //! Minimum size for client area. Read-only cache value.
		Size maximum_size; //! Maximum size for client area. Read-only cache value.


		std::string window_title; //! Window title. Read-only cache value.

		float ppi = 96.0f;
		float pixel_ratio = 0.0f;	// 0.0f = Unset
	};
}
