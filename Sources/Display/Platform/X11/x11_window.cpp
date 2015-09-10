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

#include "Display/precomp.h"

#include "API/Core/Text/logger.h"

#include "x11_window.h"

#ifndef _NET_WM_STATE_REMOVE
#define _NET_WM_STATE_REMOVE  0
#define _NET_WM_STATE_ADD     1
#define _NET_WM_STATE_TOGGLE  2
#endif

constexpr int _ResizeMinimumSize_ = 8;

namespace clan
{
	X11Window::X11Window()
		: handle(), site(NULL), colormap(0)
	{
	}

	X11Window::~X11Window()
	{
		this->destroy();
		SetupDisplay::get_message_queue()->remove_client(this);
	}

	void X11Window::prepare()
	{
		float px = xGetScreenSize_px().width;
		float mm = xGetScreenSize_mm().width;

		// Get DPI of screen or use 96.0f if server doesn't have a value.
		ppi = (mm < 24) ? 96.0f : (25.4f * px / mm);
		set_pixel_ratio(this->pixel_ratio);
	}

	void X11Window::create(XVisualInfo *visual, DisplayWindowSite *site, const DisplayWindowDescription &desc)
	{
		// Setup the handle and site.
		this->handle = DisplayWindowHandle {
			.display = visual->display,
			.window  = 0,
			.screen  = visual->screen
		};

		this->site = site;

		prepare();

		// Retrieve the root window.
		auto root_window = RootWindow(display, screen);

		// Create a brand new colormap.
		colormap = XCreateColormap(handle.display, root_window, visual->visual, AllocNone);

		// Setup basic window attributes.
		XSetWindowAttributes window_attributes = XSetWindowAttributes
		{
			.background_pixmap      = None,
			.background_pixel       = 0xFF0E0E0Eul,
			.border_pixmap          = CopyFromParent,
			.border_pixel           = 0x00000000ul,
			.bit_gravity            = NorthWestGravity, // Always retain top-left corner.
			.win_gravity            = NorthWestGravity, // Always maintain windows relative to top-left position.
			.backing_store          = WhenMapped,
			.backing_planes         = -1ul,
			.backing_pixel          =  0ul,
			.save_under             = False,
			.event_mask             = ExposureMask | StructureNotifyMask | FocusChangeMask | PropertyChangeMask, // TODO Everything needed
			.do_not_propagate_mask  = NoEventMask,
			.override_redirect      = False,
			.colormap               = this->colormap,
			.cursor                 = None
		};

		// Calculate the size of the window to be created.
		if (desc.is_position_client_area() == false)
			throw Exception("window frame area positioning is not supported on X11Window");

		Size window_size = desc.get_size() * pixel_ratio;

		// Minimum clamp (to avoid negative sizes).
		window_size.width  = std::max(_ResizeMinimumSize_, window_size.width);
		window_size.height = std::max(_ResizeMinimumSize_, window_size.height);


		// Create the X11 window.
		// - Ignore the starting window position because modern WMs will reset them anyway.
		// - Use the window width and height supplied by callee.
		// - No border width.
		// - Use XVisualInfo supplied by callee.
		// - Apply all window attributes we specified.
		handle.window = XCreateWindow(
				handle.display, root_window, 0, 0, static_cast<unsigned int>(window_size.width), static_cast<unsigned int>(window_size.height), 0
				visual->depth, visual->c_class, visual->visual,
				CWBackPixmap | CWBackPixel | CWBorderPixmap | CWBorderPixel | CWBitGravity | CWWinGravity |
				CWBackingStore | CWBackingPlanes | CWBackingPixel | CWSaveUnder | CWEventMask | CWDontPropagate |
				CWColormap | CWCursor, &window_attributes
				);

		// Update last_size.
		this->last_size = window_size;

		// Calculate minimum and maximum size of the window.
		// Clamp to current window size if resize not allowed.
		minimum_size = (!desc.get_allow_resize()) ? window_size : Size { _ResizeMinimumSize_, _ResizeMinimumSize_ };
		maximum_size = (!desc.get_allow_resize()) ? window_size : Size { -1, -1 }; // No maximum size if resize is allowed.

		// Set up WMNormalHints. TODO Does this leak?
		this->size_hints = XAllocSizeHints();
		assert(this->size_hints != NULL);

		// Overwrite default WMNormalHints.
		{	// Get WMNormalHints for the window.
			long supplied_return, _all_size_hint_flags_ = USPosition | USSize | PPosition | PSize | PMinSize | PMaxSize | PResizeInc | PAspect | PBaseSize | PWinGravity;
			Status result = XGetWMNormalHints(handle.display, handle.window, size_hints, &supplied_return);
			assert( result == 0 ); // Check returned Status code.
			aseert((supplied_return & _all_size_hint_flags_) != 0);
		}

		size_hints->flags = PResizeInc | PBaseSize | PWinGravity | PMinSize | (desc.get_allow_resize()) ? PMaxSize : 0;
		size_hints->min_width   = minimum_size.width;
		size_hints->min_height  = minimum_size.height;
		size_hints->max_width   = maximum_size.width;
		size_hints->max_height  = maximum_size.height;
		size_hints->width_inc   = 1;
		size_hints->height_inc  = 1;
		// TODO awesomeWM ignores these in favour of values supplied on XCreateWindow().
		//      Test this on KDE and Xfwm.
		size_hints->base_width  = window_size.width;
		size_hints->base_height = window_size.height;
		size_hints->win_gravity = NorthWestGravity;

		// Set new WMNormalHints.
		XSetWMNormalHints(handle.display, handle.window, size_hints);

	} // end def fn X11Window::create

	void X11Window::destroy()
	{
		// Clear cached values.
		minimum_size = Size();
		maximum_size = Size();
		window_title = "";
	}


	////

	void X11Window::map_window()
	{
		XMapWindow(handle.display, handle.window);
		XFlush(display);

		set_position(last_size);
	}

	void X11Window::unmap_window()
	{
		throw new Exception("not yet unimplemented");
	}

	void set_position(const Point &new_pos)
	{
		if (xGetWindowAttributes().map_state == IsUnmapped)
			throw Exception("X11Window:set_size() failed: Window is unmapped.");

		XMoveWindow(handle.display, handle.window, new_pos.x, new_pos.y);
		last_position = new_pos;
	}

	void set_size(const Size &new_size)
	{
		if (xGetWindowAttributes().map_state == IsUnmapped)
			throw Exception("X11Window:set_size() failed: Window is unmapped.");

		XResizeWindow(handle.display, handle.window, new_size.width, new_size.height);
		last_size = new_size;
	}


	void set_pixel_ratio(float new_ratio)
	{
		pixel_ratio = ratio;

		// Pixel ratio is not set; calculate pixel ratio closes to current PPI.
		if (pixel_ratio == 0.0f)
		{
			int s = std::round(ppi / 16.0f);
			/**/ if (s <= 6)  // <=  96 PPI; old tech; use 1:1 ratio.
			{
				pixel_ratio = 1.0f;
			}
			else if (s >= 12) // >= 192 PPI; new tech; use 1:1 ratio to avoid sub-pixeling.
			{
				pixel_ratio = static_cast<float>(s / 6);
			}
			else // 96 ~ 192 PPI; modern; use one-sixth steps
			{
				pixel_ratio = static_cast<float>(s) / 6.0f;
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	//  X11 Function Wrappers
	////////////////////////////////////////////////////////////////////////////
	XWindowAttributes X11Window::xGetWindowAttributes()
	{
		XWindowAttributes attr;
		Status ret = XGetWindowAttributes(handle.display, handle.window, &attr);
		if (ret == 0)
			throw Exception("X11Window::xGetWindowAttributes() failed");
		else
			return attr;
	}

	Size X11Window::xGetScreenSize_px()
	{
		return Size {
			XDisplayWidth(handle.display, handle.screen),
			XDisplayHeight(handle.display, handle.screen)
		};
	}

	Size X11Window::xGetScreenSize_mm()
	{
		return Size {
			XDisplayWidthMM(handle.display, handle.screen),
			XDisplayHeightMM(handle.display, handle.screen)
		};
	}
}
