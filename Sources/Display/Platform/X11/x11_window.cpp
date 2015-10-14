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

#include "../../setup_display.h"

#include "display_message_queue_x11.h"

#include "input_device_provider_x11keyboard.h"
#include "input_device_provider_x11mouse.h"
#ifdef HAVE_LINUX_JOYSTICK_H
#include "input_device_provider_linuxjoystick.h"
#endif

#include <cassert>
#include <chrono> // std::chrono::milliseconds in map_window()
#include <new> // std::bad_alloc on xStringListToTextProperty()
#include <thread> // std::this_thread in map_window()
#include <unistd.h> // getpid(), gethostname(),

#include <X11/Xatom.h> // XA_CARDINAL
#include <X11/XKBlib.h> // XkbSetDetectableAutoRepeat()

#ifndef _NET_WM_STATE_REMOVE
#define _NET_WM_STATE_REMOVE  0
#define _NET_WM_STATE_ADD     1
#define _NET_WM_STATE_TOGGLE  2
#endif

//! Use the (annoyingly more complicated) XSetWMClientMachine convenience
//! function over XChangeProperty.
#define CL_X11_USE_XSetWMClientMachine

//! Use the client window coordinates obtained from XGetWindowAttribute as
//! origin coordinates in parent-window-space when calculating the root/screen
//! coordinates of the top-left pixel of the client window. (old behaviour)
//! The alternative is to simply translate Point(0, 0) in client-window-space
//! to root-window-space.
// #define CL_X11_USE_XWindowAttribute_COORDINATES_WHEN_TRANSLATING

namespace clan
{
	//! Minimum resize size clamp value.
	constexpr int _ResizeMinimumSize_ = 8;

	//! Maximum resize size clamp value.
	constexpr int _ResizeMaximumSize_ = 32768; // a 32Kx32K display; by then X should've been dead. 

	//! XWindowAttribute::event_mask for clan::Display::set_enabled().
	constexpr long xWinAttrEventMaskWhenDisabled = 
		EnterWindowMask | LeaveWindowMask | KeymapStateMask | ExposureMask |
		StructureNotifyMask | FocusChangeMask | PropertyChangeMask;

	constexpr long xWinAttrEventMaskWhenEnabled = 
		KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
		PointerMotionMask | xWinAttrEventMaskWhenDisabled;


	X11Window::X11Window()
		: handle(), site(NULL), atoms(), colormap(0), size_hints(NULL)
	{
		handle.display = SetupDisplay::get_message_queue()->get_display();

		keyboard = InputDevice(new InputDeviceProvider_X11Keyboard(this));
		mouse    = InputDevice(new InputDeviceProvider_X11Mouse   (this));
		// TODO Joystick???

		// TODO Make caller add the client by itself after calling ctor.
		SetupDisplay::get_message_queue()->add_client(this);
	}

	X11Window::~X11Window()
	{
		// TODO Make caller remove the client by itself before calling dtor.
		SetupDisplay::get_message_queue()->remove_client(this);
		SetupDisplay::get_message_queue()->set_mouse_capture(this, false);

		keyboard.get_provider()->dispose();
		mouse   .get_provider()->dispose();

		for(auto &elem : joysticks)
		{
			elem.get_provider()->dispose();
		}

		this->destroy();
	}

	void X11Window::prepare()
	{
		float px = xGetScreenSize_px().width;
		float mm = xGetScreenSize_mm().width;

		// Get DPI of screen or use 96.0f if server doesn't have a value.
		ppi = (mm < 24) ? 96.0f : (25.4f * px / mm);
		set_pixel_ratio(this->pixel_ratio);

		// Load X11 Atoms.
		atoms = X11Atoms(handle.display, handle.screen);
	}

	void X11Window::create(XVisualInfo *visual, DisplayWindowSite *site, const DisplayWindowDescription &desc)
	{
		// Setup the handle and site.
		handle.screen = visual->screen;

		site = site;

		prepare();

		// Retrieve the root window.
		auto root_window = RootWindow(handle.display, handle.screen);

		// Create a brand new colormap for the window.
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
									// desc.has_no_activate() // Activate only if described
									//	? xWinAttrEventMaskWhenDisabled
									//	: xWinAttrEventMaskWhenEnabled,
			.do_not_propagate_mask  = NoEventMask,
			.override_redirect      = False,
			.colormap               = this->colormap,
			.cursor                 = None
		};

		// Calculate the size of the window to be created.
		if (desc.get_position_client_area() == false) // TODO Rename to "is_position_client_area"
			throw Exception("Window frame area positioning is not supported on X11Window");

		Size window_size = Size {
			desc.get_size().width  * pixel_ratio,
			desc.get_size().height * pixel_ratio
		};

		// Minimum size clamping (to avoid negative sizes).
		window_size.width  = std::max(_ResizeMinimumSize_, window_size.width);
		window_size.height = std::max(_ResizeMinimumSize_, window_size.height);


		// Create the X11 window.
		// - Ignore the starting window position because modern WMs will reset
		//   them anyway.
		// - Use the window width and height supplied by callee. Some WMs will
		//   favour size values here over those on WMNormalHints later.
		// - No X11 border width.
		// - Use XVisualInfo supplied by callee.
		// - Apply all window attributes we specified.
		// - Force InputOutput window class. Never use visual->c_class, because
		//   using that will fail for no damn reason.
		handle.window = XCreateWindow(
				handle.display, root_window, 0, 0, // display, window, x, y
				static_cast<unsigned int>(window_size.width), static_cast<unsigned int>(window_size.height), 0, // width, height, border_width
				visual->depth, InputOutput/* visual->c_class */, visual->visual,
				CWBackPixmap | CWBackPixel | CWBorderPixmap | CWBorderPixel | CWBitGravity | CWWinGravity |
				CWBackingStore | CWBackingPlanes | CWBackingPixel | CWSaveUnder | CWEventMask | CWDontPropagate |
				CWColormap | CWCursor, &window_attributes
				);

		log_event("debug", "clan::X11Window::create(): Running XCreateWindow with size %1x%2.", window_size.width, window_size.height);

		XFlush(handle.display);

		if (!handle.window)
			throw Exception("Failed to create the X11 window.");

		// Set the title of the window.
		set_title(desc.get_title());

		// Set the owner of this window if described so.
		auto owner = desc.get_owner();
		if (!owner.is_null())
			XSetTransientForHint(handle.display, handle.window, owner.get_handle().window);

		// Update last_size.
		this->last_size = window_size;

		// Calculate minimum and maximum size of the window.
		// Clamp to current window size if resize not allowed.
		minimum_size = (!desc.get_allow_resize()) ? window_size : Size { _ResizeMinimumSize_, _ResizeMinimumSize_ };
		maximum_size = (!desc.get_allow_resize()) ? window_size : Size { _ResizeMaximumSize_, _ResizeMaximumSize_ };

		xGetWMNormalHints();

		size_hints->flags = PResizeInc | PBaseSize | PWinGravity | PMinSize | PMaxSize;
		size_hints->min_width   = minimum_size.width;
		size_hints->min_height  = minimum_size.height;
		size_hints->max_width   = maximum_size.width;
		size_hints->max_height  = maximum_size.height;
		size_hints->width_inc   = 1;
		size_hints->height_inc  = 1;
		// Note: Some WMs will ignore these and favour the size supplied on XCreateWindow().
		size_hints->base_width  = window_size.width;
		size_hints->base_height = window_size.height;
		size_hints->win_gravity = NorthWestGravity;

		// Set new WMNormalHints.
		xSetWMNormalHints();

		{	// Inform the window manager who we are, so that it can kill us if
			// we are not good for its universe.
			if (!atoms.exists("_NET_WM_PID") || !atoms.exists("WM_CLIENT_MACHINE"))
				throw Exception("Missing basic X11 atoms.");

			Atom atom = None;

			int32_t pid = getpid();
			if (pid > 0)
			{
				atom = atoms.get_atom(handle.display, "_NET_WM_PID", False);
				XChangeProperty(handle.display, handle.window, atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *) &pid, 1);
			}
			
			char hostname[256];
			if (gethostname(hostname, sizeof(hostname)) > -1)
			{
				hostname[255] = 0;

#ifndef CL_X11_USE_XSetWMClientMachine
				atom = atoms.get_atom(handle.display, "WM_CLIENT_MACHINE", False);
				XChangeProperty(handle.display, handle.window, atom, XA_STRING, 8, PropModeReplace, (unsigned char *) hostname, strlen(hostname));
#else
				XTextProperty text_prop = xStringListToTextProperty({{ hostname }});

				XSetWMClientMachine(handle.display, handle.window, &text_prop);

				if (text_prop.value != NULL)
					XFree(text_prop.value);
#endif
			}
		}

		// TODO Cursor code...
		// TODO Window type/styling...
		{	// Subscribe to WM events.
			Atom protocol = atoms["WM_DELETE_WINDOW"];
			Status result = XSetWMProtocols(handle.display, handle.window, &protocol, 1);
			if (result == 0)
				log_event("debug", "clan::X11Window::create(): Failed to set WM_DELETE_WINDOW protocol.");
		}

		// Set up keyboard auto-repeat.
		Bool supports_DAR = False;
		XkbSetDetectableAutoRepeat(handle.display, True, &supports_DAR);
		if (supports_DAR == False)
			log_event("debug", "X11Window::create() failed: Could not set keyboard auto-repeat.");

		// TODO Set up joysticks

		{ // Figure out window position.
			this->last_position = desc.is_fullscreen() 
				? Point { 0, 0 }
				: Point { desc.get_position().left, desc.get_position().top };

			Size screen_size = xGetScreenSize_px();

			// NOTE This is inaccurate because it doesn't consider frame extents
			// TODO Query _NET_REQUEST_FRAME_EXTENTS. If not available, query
			//      _NET_FRAME_EXTENTS after map_window().
			if (last_position.x == -1)
				last_position.x = (screen_size.width  - last_size.width ) / 2 - 1;

			if (last_position.y == -1)
				last_position.y = (screen_size.height - last_size.height) / 2 - 1;
		}

		// Set window visibility as described.
		if (desc.is_visible())
			show(false); // Show window but don't activate.
		
		// Make window fullscreen if requested.
		if (desc.is_fullscreen())
			set_fullscreen(true);

		// TODO Setup clipboard?

	} // end def fn X11Window::create

	void X11Window::destroy()
	{
		// Clear cached values.
		last_position = Point();
		last_size = Size();

		minimum_size = Size();
		maximum_size = Size();

		window_title = "";

		// Destroy
		if (handle.window != 0)
		{
			XDestroyWindow(handle.display, handle.window);
			handle.window = 0;
			handle.screen = -1;
		}

		if (colormap != 0)
		{
			XFreeColormap(handle.display, colormap);
			colormap = 0;
		}

		if (size_hints != NULL)
		{
			XFree(size_hints);
			size_hints = NULL;
		}

		atoms.clear();
	}

	////

	bool X11Window::has_focus() const
	{
		Window focus_window;
		int    focus_state;
		XGetInputFocus(handle.display, &focus_window, &focus_state);
		// assert(focus_state == RevertToParent); // TODO This should always revert to parent?
		return (focus_window == handle.window);
	}

	bool X11Window::is_fullscreen() const
	{
		// Check if _NET_WM_STATE exists.
		if (!atoms.is_hint_supported("_NET_WM_STATE") || !atoms.is_hint_supported("_NET_WM_STATE_FULLSCREEN"))
		{
			log_event("debug", "clan::X11Window::set_fullscreen() failed: EWMH _NET_WM_STATE_FULLSCREEN not available.");
			return false;
		}

		// Check if _NET_WM_STATE_FULLSCREEN is currently set.
		auto ret = atoms.check_net_wm_state(handle.window, { "_NET_WM_STATE_FULLSCREEN" });
		return (ret.size() == 1) ? ret[0] : false;
	}

	bool X11Window::is_minimized() const
	{
		if (xGetWindowAttributes().map_state == IsUnmapped) // TODO Should we abort if window is unmapped? See ICCCM sec. 4.1.3.1
			log_event("debug", "clan::X11Window::is_minimized() warning: Window is unmapped.");

		// Check EWMH specified _NET_WM_STATE first.
		auto ret = atoms.check_net_wm_state(handle.window, { "_NET_WM_STATE_HIDDEN" } );
		if (ret.size() == 1)
		{
			return ret.front();
		}
		
		// If not available, check Xlib WM_STATE property
		assert(atoms.exists("WM_STATE"));

		unsigned long  item_count;
		unsigned char *data = atoms.get_property(handle.window, "WM_STATE", item_count);
		if (data != NULL)
		{
			long state = *(long *)data;
			XFree(data);
			return state == IconicState;
		}
		else
		{
			log_event("debug", "clan::X11Window::is_minimized() -> false: Failed to get WM_STATE property.");
			return false; // Window may be Withdrawn, hence no WM_STATE property.
		}
	}

	bool X11Window::is_maximized() const
	{
		if (xGetWindowAttributes().map_state == IsUnmapped)
			log_event("debug", "clan::X11Window::is_maximized() warning: Window is unmapped.");
		
		auto ret = atoms.check_net_wm_state(handle.window, { "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_MAXIMIZED_VERT" });
		if (ret[0] != ret[1])
			log_event("debug", "clan::X11Window::is_maximized() -> false: Window is only maximized on the %1 side.", ret[0] ? "horizontal" : "vertical");

		return ret[0] && ret[1];
	}

	////

	void X11Window::map_window()
	{
		if (xGetWindowAttributes().map_state != IsUnmapped)
			throw Exception("X11Window::unmap_window() failed: Window already in mapped state.");

		// Map the window.
		XMapWindow(handle.display, handle.window);
		XFlush(handle.display);

		// Give WM some time to map the window.
		std::this_thread::sleep_for( std::chrono::milliseconds(500) );

		XFlush(handle.display);
		// TODO only call set_position when map window event is received.
		try
		{
			// Set the window position.
			set_position(last_position);
			set_size(last_size);
		}
		catch (Exception &e)
		{
			if (std::string(e.what()).find("unmapped") != std::string::npos)
			{
				log_event("debug", "clan::X11Window::map_window() failure: ");
				log_event("debug", "    The system took too long to map the window.");
				log_event("debug", "    Please report this to ClanLib.");
			}
			else
			{
				throw e;
			}
		}
	}

	void X11Window::unmap_window()
	{
		if (xGetWindowAttributes().map_state == IsUnmapped)
			throw Exception("X11Window::unmap_window() failed: Window already in unmapped state.");

		XUnmapWindow(handle.display, handle.window);
		XFlush(handle.display);
	}

	void X11Window::refresh_client_window_attributes()
	{
		Window  root;
		Window  parent;
		Window  child;
		Window* children;
		unsigned int children_count;

		// Get parent Window
		XQueryTree(handle.display, handle.window, &root, &parent, &children, &children_count);
		XFree(children);

		XWindowAttributes attr = xGetWindowAttributes();

		int xpos;
		int ypos;

#ifdef CL_X11_USE_XWindowAttribute_COORDINATES_WHEN_TRANSLATING
		// Translate this window's coordinates taken from its XWindowAttribute
		// (this coordinate is relative to parent), into root coordinate.
		XTranslateCoordinates(handle.display, parent, root, attr.x, attr.y, &xpos, &ypos, &child);
#else
		// We could also simply translate the (0,0) coordinate from this window
		// to root window for the same effect.
		XTranslateCoordinates(handle.display, handle.window, root, 0, 0, &xpos, &ypos, &child);
#endif
		client_window_position.x = xpos,
		client_window_position.y = ypos;

		client_window_size.width  = attr.width,
		client_window_size.height = attr.height;
	}

	////

	void X11Window::set_position(const Point &new_pos)
	{
		if (xGetWindowAttributes().map_state == IsUnmapped)
			throw Exception("Window is unmapped.");

		// This will cause a ConfigureNotify event to be sent.
		XMoveWindow(handle.display, handle.window, new_pos.x, new_pos.y);
		last_position = new_pos;
	}

	void X11Window::set_size(const Size &new_size)
	{
		if (xGetWindowAttributes().map_state == IsUnmapped)
			throw Exception("Window is unmapped.");

		// This will cause a ConfigureNotify event to be sent.
		XResizeWindow(handle.display, handle.window, new_size.width, new_size.height);
		last_size = new_size;
	}

	void X11Window::set_minimum_size(const Size &new_size)
	{
		xGetWMNormalHints();
		size_hints->flags = PMinSize;
		size_hints->min_width   = new_size.width;
		size_hints->min_height  = new_size.height;
		xSetWMNormalHints();
		maximum_size = new_size;
	}

	void X11Window::set_maximum_size(const Size &new_size)
	{
		xGetWMNormalHints();
		size_hints->flags = PMaxSize;
		size_hints->max_width   = new_size.width;
		size_hints->max_height  = new_size.height;
		xSetWMNormalHints();
		maximum_size = new_size;
	}



	void X11Window::set_pixel_ratio(float new_ratio)
	{
		pixel_ratio = new_ratio;

		// Pixel ratio is not set; calculate pixel ratio closest to current PPI.
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

	void X11Window::set_title(const std::string &new_title)
	{
		this->window_title = new_title;
		XStoreName(handle.display, handle.window, window_title.c_str());
	}

	void X11Window::set_fullscreen(bool new_state)
	{
		// Check if _NET_WM_STATE exists.
		if (!atoms.is_hint_supported("_NET_WM_STATE") || !atoms.is_hint_supported("_NET_WM_STATE_FULLSCREEN"))
		{
			log_event("debug", "clan::X11Window::set_fullscreen() failed: EWMH _NET_WM_STATE_FULLSCREEN not available.");
			return;
		}

		// Check if _NET_WM_STATE_FULLSCREEN is currently set.
		auto ret = atoms.check_net_wm_state(handle.window, { "_NET_WM_STATE_FULLSCREEN" });
		assert(ret.size() != 0); // TODO Clean me up.
		bool curr_state = ret[0];
		if (curr_state == new_state)
		{
			log_event(
					"debug",
					"clan::X11Window::set_fullscreen(%1) ignored: Window already %2 full-screen state.",
					(new_state ? "true" : "false"),
					(new_state ? "in" : "not in")
					);
			return;
		}

		// Set new full-screen state.
		bool success = atoms.modify_net_wm_state(
				handle.window,
				new_state ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE,
				"_NET_WM_STATE_FULLSCREEN"
				);
		
		if (!success)
			log_event("debug", "clan::X11Window::set_fullscreen(%1) failed.", new_state ? "true" : "false");

		return;
	}


	void X11Window::set_enabled(bool new_state)
	{
		XSetWindowAttributes attr; // TODO Check me once create() is done
		attr.event_mask = new_state ? xWinAttrEventMaskWhenEnabled : xWinAttrEventMaskWhenDisabled;
		XChangeWindowAttributes(handle.display, handle.window, CWEventMask, &attr);
	}

	void X11Window::minimize()
	{
		if (xGetWindowAttributes().map_state == IsUnmapped)
			throw Exception("Cannot minimize when window is unmapped.");

		Status ret = XIconifyWindow(handle.display, handle.window, handle.screen);
		if (ret == 0)
			log_event("debug", "clan::X11Window::set_minimized() failed: XIconifyWindow returns zero status.");
	}

	void X11Window::maximize()
	{
		atoms.modify_net_wm_state(handle.window, _NET_WM_STATE_ADD, { "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_MAXIMIZED_VERT" });
	}

	void X11Window::restore()
	{
		if (is_minimized())
		{
			map_window();
		}
		else if (is_maximized())
		{
			atoms.modify_net_wm_state(handle.window, _NET_WM_STATE_REMOVE, { "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_MAXIMIZED_VERT" });
		}
	}

	void X11Window::bring_to_front()
	{
		XRaiseWindow(handle.display, handle.window);
	}

	void X11Window::show(bool activate)
	{
		map_window();
		if (activate)
			set_enabled(true);
	}

	void X11Window::hide()
	{
		set_enabled(false);
		XWithdrawWindow(handle.display, handle.window, handle.screen);
	}

	void X11Window::request_repaint()
	{
		XClearArea(handle.display, handle.window, 0, 0, 1, 1, true);
		XFlush(handle.display);
		// TODO Generate ExposeEvents, cache them in MQ, then paint.
		// I'm doing it this way because this doesn't clutter the MQ.
	}

	void X11Window::capture_mouse(bool new_state)
	{
		SetupDisplay::get_message_queue()->set_mouse_capture(this, new_state);
	}

	////
	
	Rect X11Window::get_screen_position() const
	{
		return Rect { client_window_position, client_window_size };
	}

	Point X11Window::client_to_screen(const Point &client) const
	{
		return Point { client_window_position.x + client.x, client_window_position.y + client.y };
	}

	Point X11Window::screen_to_client(const Point &screen) const
	{
		return Point { screen.x - client_window_position.x, screen.y - client_window_position.y };
	}

	////////////////////////////////////////////////////////////////////////////
	//  X11 Function Wrappers
	////////////////////////////////////////////////////////////////////////////
	XWindowAttributes X11Window::xGetWindowAttributes() const
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

	void X11Window::xGetWMNormalHints()
	{
		if (size_hints == NULL)
		{
			size_hints = XAllocSizeHints();
			assert(size_hints != NULL);
		}

		long supplied_return;
		Status result = XGetWMNormalHints(handle.display, handle.window, size_hints, &supplied_return);

		if (result == 0) // Uninitialized size_hints.
			size_hints->flags = PSize;
	}

	void X11Window::xSetWMNormalHints()
	{
		assert(size_hints != NULL);
		XSetWMNormalHints(handle.display, handle.window, size_hints);
	}


	XTextProperty X11Window::xStringListToTextProperty(const std::vector< std::string > &string_list)
	{
		std::vector< std::vector<char> > cstrs;
		std::vector< char* >             cstr_list;

		// Copy convert strings inside string_list to vector<char> and insert that into cstrs.
		for (const auto &str : string_list)
			cstrs.emplace_back(str.cbegin(), str.cend());

		// Take char pointers of strings in cstrs and save that into cstr_list.
		for (auto &str : cstrs)
			cstr_list.push_back(str.data());

		XTextProperty text_prop;
		int success = XStringListToTextProperty(cstr_list.data(), 1, &text_prop);
		if (success == 0)
			throw std::bad_alloc(); // Failed to convert string to XTextProperty.

		return text_prop;
	}
}
