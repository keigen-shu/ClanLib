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
#include <unistd.h> // getpid(), gethostname(), access()

#include <X11/Xatom.h> // XA_CARDINAL
#include <X11/XKBlib.h> // XkbSetDetectableAutoRepeat()
#include <X11/cursorfont.h> // XC_...

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
		, system_cursor(0), invisible_cursor(0), invisible_pixmap(0)
	{
		handle.display = SetupDisplay::get_message_queue()->get_display();

		keyboard = InputDevice(new InputDeviceProvider_X11Keyboard(this));
		mouse    = InputDevice(new InputDeviceProvider_X11Mouse   (this));

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
			elem.get_provider()->dispose();

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

		Size window_size = Size(
			float(desc.get_size().width ) * pixel_ratio,
			float(desc.get_size().height) * pixel_ratio
			);

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
			if (!atoms.is_hint_supported("_NET_WM_PID") || !atoms.exists("WM_CLIENT_MACHINE"))
				throw Exception("Missing basic X11 atoms.");

			Atom atom = None;

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

			int32_t pid = getpid();
			if (pid > 0)
			{
				atom = atoms.get_atom(handle.display, "_NET_WM_PID", False);
				XChangeProperty(handle.display, handle.window, atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *) &pid, 1);
			}
		}

		// TODO Window type/styling...
		
		{   // Set up WM_HINTS and WM_PROTOCOLS
			XWMHints wm_hints = XWMHints {
				.flags = InputHint | StateHint,
				.input = (desc.has_no_activate() ? False : True), // See ICCCM §4.1.7
				.initial_state = (desc.is_visible() ? NormalState : WithdrawnState),
				.icon_pixmap = 0, // unused
				.icon_window = 0, // unused
				.icon_x = 0, .icon_y = 0, // unused
				.icon_mask = 0, // unused
				.window_group = 0 // unused
			};

			XSetWMHints(handle.display, handle.window, &wm_hints);

			// Setup window protocols.
			// We don't need to include WM_TAKE_FOCUS (ICCCM §4.1.7) because our
			// windows either don't accept input or let WM to decide focus.

			// Subscribe to WM_DELETE_WINDOW and _NET_WM_PING events.
			std::vector<Atom> protocols;

			if (atoms.exists("WM_DELETE_WINDOW"))
				protocols.emplace_back(atoms["WM_DELETE_WINDOW"]);

			if (atoms.is_hint_supported("_NET_WM_PING"))
				protocols.emplace_back(atoms["_NET_WM_PING"]);

			Status result = XSetWMProtocols(handle.display, handle.window, protocols.data(), protocols.size());
			if (result == 0)
				log_event("debug", "clan::X11Window::create(): Failed to set WM protocols.");
		}

		// Set up keyboard auto-repeat.
		Bool supports_DAR = False;
		XkbSetDetectableAutoRepeat(handle.display, True, &supports_DAR);
		if (supports_DAR == False)
			log_event("debug", "X11Window::create(): Failed to set keyboard auto-repeat.");

		// Set up joysticks
		for (auto &elem : joysticks)
		{
			elem.get_provider()->dispose();
		}

		joysticks.clear();
#ifdef HAVE_LINUX_JOYSTICK_H
		{
			const std::string joydev = std::string {
				(access("/dev/input/", R_OK | X_OK) == 0)
				? "/dev/input/js%1"
				: "/dev/js%1"
			};

			constexpr int _MaxJoysticks_ = 16;
			for (int i = 0; i < _MaxJoysticks_; ++i)
			{
				std::string path = string_format(joydev, i);
				if (access(path.c_str(), R_OK) == 0)
				{
					try
					{
						auto joystick_provider = new InputDeviceProvider_LinuxJoystick(this, path);
						joysticks.emplace_back(joystick_provider);

						// TODO
						// current_window_events.push_back(joystick_provider->get_fd());
					}
					catch (Exception &e)
					{
						log_event("debug", "clan::X11Window::create(): Failed to initialize joystick '%1'", path);
						log_event("debug", "    reason: %1", e.message);
					}
				}
			}
		}
#endif

		{ // Figure out window position.
			this->last_position = desc.is_fullscreen() 
				? Point(0, 0)
				: Point(desc.get_position().left, desc.get_position().top);

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

		if (system_cursor != 0)
		{
			XFreeCursor(handle.display, system_cursor);
			system_cursor = 0;
		}

		if (invisible_cursor != 0)
		{
			XFreeCursor(handle.display, invisible_cursor);
			invisible_cursor = 0;
		}

		if (invisible_pixmap != 0)
		{
			XFreePixmap(handle.display, invisible_pixmap);
			invisible_pixmap = 0;
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
		// TODO Should this always be RevertToParent?
		// assert(focus_state == RevertToParent); 
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
		// TODO Should we just abort if window is unmapped? See ICCCM §4.1.3.1
		if (xGetWindowAttributes().map_state == IsUnmapped) 
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
			throw Exception("Window already in mapped state.");

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
			throw Exception("Window already in unmapped state.");

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

	void X11Window::show_system_cursor()
	{
		if (system_cursor == 0)
		{
			system_cursor = XCreateFontCursor(handle.display, XC_left_ptr);
		}

		XDefineCursor(handle.display, handle.window, system_cursor);
	}

	void X11Window::hide_system_cursor()
	{
		if (invisible_pixmap == 0)
		{   // Set-up an invisible pixmap.
			char pixmap_data[] = { 0 };
			invisible_pixmap = XCreateBitmapFromData(handle.display, handle.window, pixmap_data, 1, 1);
		}

		if (invisible_cursor == 0)
		{
			XColor blank_color;
			memset(&blank_color, 0, sizeof(blank_color));
			invisible_cursor = XCreatePixmapCursor(
					handle.display, invisible_pixmap, invisible_pixmap,
					&blank_color, &blank_color, 0, 0
					);
		}

		XDefineCursor(handle.display, handle.window, invisible_cursor);
	}

	void X11Window::set_cursor(StandardCursor type)
	{
		if (system_cursor != 0)
		{
			XFreeCursor(handle.display, system_cursor);
			system_cursor = 0;
		}

		XID index = XC_left_ptr;
		switch (type)
		{
			case StandardCursor::arrow:
				index = XC_left_ptr;
				break;
			case StandardCursor::appstarting:
				index = XC_watch;
				break;
			case StandardCursor::cross:
				index = XC_cross;
				break;
			case StandardCursor::hand:
				index = XC_hand2;
				break;
			case StandardCursor::ibeam:
				index = XC_xterm;
				break;
			case StandardCursor::size_all:
				index = XC_fleur;
				break;
			case StandardCursor::size_ns:
				index = XC_double_arrow;
				break;
			case StandardCursor::size_we:
				index = XC_sb_h_double_arrow;
				break;
			case StandardCursor::uparrow:
				index = XC_sb_up_arrow;
				break;
			case StandardCursor::wait:
				index = XC_watch;
				break;
			case StandardCursor::no:
				index = XC_X_cursor;
				break;
			case StandardCursor::size_nesw: // TODO Expand to size_ne, size_sw
			case StandardCursor::size_nwse: // TODO Expand to size_nw, size_se
			default:
				break;
		}

		system_cursor = XCreateFontCursor(handle.display, index);
		XDefineCursor(handle.display, handle.window, system_cursor);
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

	////////////////////////////////////////////////////////////////////////////
	// X11 Window Event Processing
	////////////////////////////////////////////////////////////////////////////
	void X11Window::process_event(XEvent &event, X11Window *mouse_capture_window)
	{
		switch(event.type)
		{
		// Keyboard events
		case KeyPress:
		case KeyRelease:
		{
			dynamic_cast<InputDeviceProvider_X11Keyboard *>(keyboard.get_provider())
				->received_keyboard_input(keyboard, event.xkey);
			break; // TODO Ugly function definition.
		}
		// Pointer events
		case ButtonPress:
		case ButtonRelease:
		{
			XButtonEvent e = event.xbutton;
			// Test if button event is on masked area. If not, let it fall
			// through our window and pass it to the window below it.
			if (fn_on_click)
			{
				if (!fn_on_click(event.xbutton))
					break;
			}

			// Translate button event position to the window capturing mouse
			// events if it is not this window.
			if (mouse_capture_window != this)
			{
				auto capturing_client_position = mouse_capture_window->client_window_position;
				e.x += client_window_position.x - capturing_client_position.x;
				e.y += client_window_position.y - capturing_client_position.y;
			}

			dynamic_cast<InputDeviceProvider_X11Mouse *>(mouse_capture_window->get_mouse().get_provider())
				->received_mouse_input(mouse_capture_window->mouse, e);
			break; // TODO Ugly function definition.
		}

		case MotionNotify:
		{
			XMotionEvent e = event.xmotion;
			// Translate button event position to the window capturing mouse
			// events if it is not this window.
			if (mouse_capture_window != this)
			{
				auto capturing_client_position = mouse_capture_window->client_window_position;
				e.x += client_window_position.x - capturing_client_position.x;
				e.y += client_window_position.y - capturing_client_position.y;
			}

			dynamic_cast<InputDeviceProvider_X11Mouse *>(mouse_capture_window->get_mouse().get_provider())
				->received_mouse_move(mouse_capture_window->mouse, e);
			break; // TODO Ugly function definition.
		}

		// Window crossing events
		case EnterNotify:
		case LeaveNotify:
		{
			XCrossingEvent e = event.xcrossing;
			// TODO DisplayWindowSite::sig_window_{enter,leave}
			break;
		}

		// Keymap state events
		case KeymapNotify:
		{	// Contains the current state of the keyboard when window receives focus.
			// TODO Update keyboard state depending on suppliedkeymap state.
			log_event("debug", "KeymapNotify event unimplemented!");
			break;
		}

		// Input focus events
		case FocusIn:
		{
			if (site)
			{
				if (has_focus()) // Make sure we really did obtain focus.
					(site->sig_got_focus)();
				else
					log_event("debug", "FocusIn event ignored: we really didn't gain focus.");
					// If this triggers, please check mode.
			}
			break;
		}
		case FocusOut:
		{
			if (site)
			{
				if (!has_focus()) // Make sure we really did lose focus.
					(site->sig_lost_focus)();
				else
					log_event("debug", "FocusOut event ignored: we really didn't gain focus.");
					// If this triggers, please check mode.
			}
			break;
		}
		// Expose events
		case Expose:
			log_event("debug", "Expose event unimplemented!"); // TODO
			break;

		// The following two events are generated and used like so:
		//
		// 1. Someone calls XCopyArea or XCopyPlane to copy graphics from a
		//    source Drawable to a destination Drawable.
		//
		// 2. If the sx, sy, dx, dy, width and height parameters supplied into
		//    these functions cause them to attempt copying from a source area
		//    that has missing content (either out-of-bounds or not available
		//    due to being unmapped or obstructed by another window),
		//    GraphicExpose events will be generated and sent [to whom?] in the
		//    hopes that something would be done about it.
		//
		//    Otherwise, if there are no problems when copying content, a
		//    NoExpose event is generated.
		//
		// Since ClanLib doesn't use these functions, it must've came from
		// another X client. In that case, we'll simply ignore the event.
		case GraphicsExpose:
			log_event("debug", "Ignored GraphicsExpose event.");
			break;
		case NoExpose:
			log_event("debug", "Ignored NoExpose event.");
			break;

		// Structure control events
		// We don't care about these. They are much more interesting to WMs.
		case CirculateRequest:
			log_event("debug", "Ignored CirculateRequest event.");
			break;
		case ConfigureRequest:
			log_event("debug", "Ignored ConfigureRequest event.");
			break;
		case MapRequest:
			log_event("debug", "Ignored MapRequest event.");
			break;
		case ResizeRequest:
			log_event("debug", "Ignored ResizeRequest event.");
			break;

		// Window state notification events
		case CirculateNotify: // Ignored; we don't circulate our own subwindows.
			// This is probably used by WMs to implement Alt-Tabbing.
			log_event("debug", "Ignored CirculateNotify event.");
			break;
		case ConfigureNotify: // Interested.
			log_event("debug", "ConfigureNotify event unimplemented!"); // TODO
			break;
		case CreateNotify: // Ignored; we do not create child windows.
			log_event("debug", "Ignored CreateNotify event.");
			break;
		case DestroyNotify: // Ignored; currently unused.
			// TODO Should we clean up attributes here rather than in destroy()?
			log_event("debug", "Ignored DestroyNotify event."); // TODO
			break;
		case GravityNotify: // Ignored; we do not have child windows.
			log_event("debug", "Ignored GravityNotify event.");
			break;
		case MapNotify: // Interested.
			// If this window is supposed to be a modal dialog, modify all other
			// top-level windows managed by ClanLib so that they will raise this
			// window when they receive events.
			log_event("debug", "MapNotify event unimplemented!"); // TODO
			break;
		case MappingNotify: // Ignored; unused.
			// We don't care about mapping changes to modifier keys on keyboard
			// (aside from Ctrl, Alt, Shift and Super) keyboard symbols (we
			// always assume US-international layout), or pointer buttons.
			log_event("debug", "Ignored MappingNotify event.");
			break;
		case ReparentNotify: // Interested if SNM. SsNM ignored because we do not create child windows.
			// We are definitely interested in WMs messing with the positioning
			// and sizing of our windows.
			// TODO Update _NET_FRAME_EXTENTS after reparenting.
			log_event("debug", "ReparentNotify event unimplemented!"); // TODO
			break;

		case UnmapNotify: // Interested.
			// If this window is a modal dialog, revert changes to all other
			// top-level windows managed by ClanLib back to normal.
			log_event("debug", "UnmapNotify event unimplemented!"); // TODO
			break;

		case VisibilityNotify: // Ignored; not interesting at the moment.
			// TODO We can use this to limit the amount of drawing.
			log_event("debug", "Visibility event unimplemented!");
			break;

		// Colormap state notification events
		case ColormapNotify: // Ignored; we don't care about colormaps; we only have one.
			log_event("debug", "Ignored ColormapNotify event.");
			break;

		// Client communication events
		case ClientMessage: // Interested. 
		{
			// TODO ICCCM §4.1.4 Normal->Iconic specification.
			// TODO ICCCM §4.1.4 Normal->Withdrawn waiting (Maybe make that code
			// scan for this event manually?)
			if (event.xclient.message_type != atoms["WM_PROTOCOLS"])
			{
				log_event("debug", "ClientMessage event ignored: unknown message type.");
				break;
			}

			unsigned long protocol = event.xclient.data.l[0];
			if (protocol == None)
			{
				log_event("debug", "ClientMessage event ignored: WM_PROTOCOLS event protocol has no data.");
				break;
			}

			if (atoms.is_hint_supported("_NET_WM_PING"))
			{
				Atom _NET_WM_PING = atoms["_NET_WM_PING"];
				if (protocol == _NET_WM_PING)
				{
					XSendEvent(handle.display, RootWindow(handle.display, handle.screen), False, SubstructureNotifyMask | SubstructureRedirectMask, &event);
					break;
				}
			}

			if (atoms.exists("WM_DELETE_WINDOW"))
			{
				Atom WM_DELETE_WINDOW = atoms["WM_DELETE_WINDOW"];
				if (protocol == WM_DELETE_WINDOW)
				{
					if (site)
					{
						// TODO This function does not close the window?
						(site->sig_window_close)();
					}
					break;
				}
			}

			log_event("debug", "ClientMessage event ignored: Unknown protocol.");
			break;
		}
		case PropertyNotify: // May be interested... TODO research
			// TODO EWMH _NET_FRAME_EXTENTS (Maybe make code querying this scan
			// for this event manually?)
			// TODO ICCCM §2?
			log_event("debug", "PropertyNotify event unimplemented!");
			break;
		case SelectionClear: // INTERESTED TODO
			log_event("debug", "SelectionClear event unimplemented!");
			break;
		case SelectionNotify: // INTERESTED TODO
			log_event("debug", "SelectionNotify event unimplemented!");
			break;
		case SelectionRequest: // INTERESTED TODO
			log_event("debug", "SelectionRequest event unimplemented!");
			break;

		default:
			log_event("debug", "Ignoring event of unknown type.");
			break;
		} // end-switch event.type
	} // end-fn process_event
}
