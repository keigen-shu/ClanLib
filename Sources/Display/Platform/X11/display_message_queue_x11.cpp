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
**    Harry Storbacka
**    Mark Page
*/

#include "Display/precomp.h"
#include "API/Core/System/databuffer.h"
#include "API/Core/System/system.h"
#include "API/Core/System/thread_local_storage.h"
#include "display_message_queue_x11.h"
#include "x11_window.h"
#include "../../setup_display.h"
#include <algorithm>
#include <dlfcn.h>

namespace clan
{
	DisplayMessageQueue_X11::DisplayMessageQueue_X11()
	{
	}

	DisplayMessageQueue_X11::~DisplayMessageQueue_X11()
	{
		if (display)
		{
			XCloseDisplay(display);
		}

		// This MUST be called after XCloseDisplay.
		// See http://www.xfree86.org/4.8.0/DRI11.html
		if (dlopen_lib_handle)
		{
			dlclose(dlopen_lib_handle);
		}
	}

	void *DisplayMessageQueue_X11::dlopen_opengl(const char *filename, int flag)
	{
		// This is a shared resource. We assume that its filename and flags will
		// never change, which makes sense in this case.
		if (!dlopen_lib_handle)
		{
			dlopen_lib_handle = ::dlopen(filename, flag);
		}
		return dlopen_lib_handle;
	}

	::Display *DisplayMessageQueue_X11::get_display()
	{
		if (!display)
		{
			display = XOpenDisplay(nullptr);
			if (!display)
				throw Exception("Could not open X11 display!");
		}
		return display;
	}

	void DisplayMessageQueue_X11::add_client(X11Window *window)
	{
		this->get_thread_data()->windows_born.push_back(window);
	}

	void DisplayMessageQueue_X11::remove_client(X11Window *window)
	{
		this->get_thread_data()->windows_died.push_back(window);
	}

	DisplayMessageQueue_X11::ThreadDataPtr DisplayMessageQueue_X11::get_thread_data()
	{
		ThreadDataPtr data = std::dynamic_pointer_cast<ThreadData>(
				ThreadLocalStorage::get_variable("DisplayMessageQueue_X11::thread_data")
				);
		if (!data)
		{
			data = ThreadDataPtr(new ThreadData);
			ThreadLocalStorage::set_variable("DisplayMessageQueue_X11::thread_data", data);
		}
		return data;
	}

	void DisplayMessageQueue_X11::set_mouse_capture(X11Window *window, bool state)
	{
		if (state)
		{
			current_mouse_capture_window = window;
		}
		else
		{
			if (current_mouse_capture_window == window)
				current_mouse_capture_window = nullptr;
		}

	}

	void DisplayMessageQueue_X11::run()
	{
		process(-1);
	}

	void DisplayMessageQueue_X11::exit()
	{
		exit_event.set();
	}

	bool DisplayMessageQueue_X11::process(int timeout_ms)
	{
		auto time_start = System::get_time();
		int x11_handle = ConnectionNumber(display);

		while (true)
		{
			process_message();

			auto time_now = System::get_time();
			int time_remaining_ms = timeout_ms - (time_now - time_start);

			struct timeval tv;
			if (time_remaining_ms > 0)
			{
				tv.tv_sec = time_remaining_ms / 1000;
				tv.tv_usec = (time_remaining_ms % 1000) * 1000;
			}
			else
			{
				tv.tv_sec = 0;
				tv.tv_usec = 0;
			}

			fd_set rfds;
			FD_ZERO(&rfds);

			FD_SET(x11_handle, &rfds);
			FD_SET(async_work_event.read_fd(), &rfds);
			FD_SET(exit_event.read_fd(), &rfds);

			int result = select(std::max(std::max(async_work_event.read_fd(), x11_handle), exit_event.read_fd()) + 1, &rfds, nullptr, nullptr, &tv);
			if (result > 0)
			{
				if (FD_ISSET(async_work_event.read_fd(), &rfds))
				{
					async_work_event.reset();
					process_async_work();
				}
				if (FD_ISSET(exit_event.read_fd(), &rfds))
				{
					exit_event.reset();
					return false;
				}
			}
			else
			{
				break;
			}
		}
		return true;
	}

	void DisplayMessageQueue_X11::post_async_work_needed()
	{
		async_work_event.set();
	}

	void DisplayMessageQueue_X11::process_message()
	{
		auto display = get_display();
		auto data    = get_thread_data();

		XEvent event;
		while (XPending(display) > 0)
		{
			XNextEvent(display, &event); // TODO Use XCheckIfEvent to select event based on window?

			::Window event_target = event.xany.window;

			auto is_target = [&](const X11Window* elem) -> bool
			{
				return elem->get_handle().window == event_target;
			};

			// Skip windows marked as dead.
			auto dead = std::find_if(data->windows_died.begin(), data->windows_died.end(), is_target);
			if (dead != data->windows_died.cend())
				continue;

			// End loop if window is newborn.
			auto born = std::find_if(data->windows_born.begin(), data->windows_born.end(), is_target);
			if (born != data->windows_born.cend())
			{
				XPutBackEvent(display, &event);
				break; // End this loop now so that windows in windows_born gets added to the main list.
			}

			// Get window from list.
			auto iter = std::find_if(data->windows.begin(), data->windows.end(), is_target);
			if (iter == data->windows.end()) // Event not in window list
#ifndef DEBUG
				continue;
#else
			{
				log_event("debug", "DisplayMessageQueue_X11::process_message(): dropping with event with unknown target window.");
				continue;
			}
#endif

			X11Window *window = *iter;
			X11Window *mouse_capture_window = (current_mouse_capture_window == nullptr) ? window : current_mouse_capture_window;

			// Process the event.
			window->process_event(event, mouse_capture_window);
		}

		// Remove dead windows.
		for (auto &elem : data->windows_died)
			std::remove(data->windows.begin(), data->windows.end(), elem);
		
		data->windows_died.clear();

		// Insert newborn windows.
		for (auto &elem : data->windows_born)
			data->windows.push_back(elem);

		data->windows_born.clear();
	}
}

