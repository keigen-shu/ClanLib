/*
**  ClanLib SDK
**  Copyright (c) 1997-2013 The ClanLib Team
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
**    Mark Page
*/

#include <ClanLib/core.h>
#include <ClanLib/application.h>
#include <ClanLib/display.h>
#include <ClanLib/gl.h>
#include <ClanLib/swrender.h>
#include <ClanLib/d3d.h>

#include <cmath>

// This is the Application class (That is instantiated by the Program Class)
class App
{
public:
	static int main(const std::vector<std::string> &args);
	int start(const std::vector<std::string> &args);

private:
	void on_mouse_down(const clan::InputEvent &key);
	void on_mouse_up(const clan::InputEvent &key);
	void on_mouse_move(const clan::InputEvent &key, clan::DisplayWindow *window);
	void on_window_close(clan::DisplayWindow *window);
	void on_lost_focus();
	void on_input_up(const clan::InputEvent &key);

private:
	int tux_radius;
	clan::Pointf tux_position;
	clan::Point last_mouse_pos;
	bool drag_start;

	bool quit;
};

int App::main(const std::vector<std::string> &args)
{
	// Initialize ClanLib base components
	clan::SetupCore setup_core;

	// Initialize the ClanLib display component
	clan::SetupDisplay setup_display;

	// We support all display targets, in order listed here
	clan::SetupD3D setup_d3d;
	clan::SetupGL setup_gl;
	clan::SetupSWRender setup_swrender;

	// Start the Application
	App app;
	int retval = app.start(args);
	return retval;
}


// Instantiate Application, informing it where the Program is located
clan::Application app(&App::main);

// The start of the Application
int App::start(const std::vector<std::string> &args)
{
	quit = false;
	drag_start = false;

	// Set the window description
	clan::DisplayWindowDescription desc_window;
	desc_window.set_title("Layered Window Example");
	desc_window.set_allow_resize(false);
	desc_window.set_layered(true);
	desc_window.show_caption(false);
	desc_window.set_size(clan::Size(600, 600), false);

	// Open the windows
	clan::DisplayWindow window(desc_window);
	clan::Slot slot_quit = window.sig_window_close().connect(this, &App::on_window_close, &window);
	clan::Slot slot_mouse_down = (window.get_ic().get_mouse()).sig_key_down().connect(this, &App::on_mouse_down);
	clan::Slot slot_mouse_dblclk = (window.get_ic().get_mouse()).sig_key_dblclk().connect(this, &App::on_mouse_down);
	clan::Slot slot_mouse_up = (window.get_ic().get_mouse()).sig_key_up().connect(this, &App::on_mouse_up);
	clan::Slot slot_mouse_move = (window.get_ic().get_mouse()).sig_pointer_move().connect(this, &App::on_mouse_move, &window);
	clan::Slot slot_lost_focus = window.sig_lost_focus().connect(this, &App::on_lost_focus);
	clan::Slot slot_input_up = (window.get_ic().get_keyboard()).sig_key_up().connect(this, &App::on_input_up);

	clan::Canvas canvas(window);

	// Get the graphics
	clan::FontDescription font_desc;
	font_desc.set_typeface_name("tahoma");
	font_desc.set_height(48);
	font_desc.set_subpixel(false);
	clan::Font font_large(canvas, font_desc);

	font_desc.set_height(30);
	clan::Font font_small(canvas, font_desc);
	clan::Sprite tux(canvas, "round_tux.png");
	tux_radius = tux.get_width()/2;

	clan::Image rock(canvas, "rock.png");

	clan::BlendStateDescription blend_desc;
	blend_desc.enable_blending(false);
	clan::BlendState blend_state_off(canvas, blend_desc);

	float rotation = 0.0f;
	clan::GameTime game_time;

	// Run until someone presses escape
	while (!quit)
	{
		game_time.update();

		canvas.clear(clan::Colorf(0.0f,0.0f,0.0f, 0.0f));
		rock.set_color(clan::Colorf(1.0f, 1.0f, 1.0f, 0.8f));
		rock.draw(canvas, 0.0f, 0.0f);

		// Rotate tux
		rotation += game_time.get_time_elapsed() / 10.0f;
		clan::Angle angle;
		angle.set_degrees(rotation);
		tux.set_angle(angle);

		// Caculate tux position
		clan::Pointf circle_center(  (float) (canvas.get_width()/2), (float) (canvas.get_height()/2) );
		const float radius = 210.0f;
		int tux_circle = 12;
		tux_position.x = -(radius - tux_radius - tux_circle)  * cos( angle.to_radians() / 2.0f );
		tux_position.y = (radius - tux_radius - tux_circle) * sin( angle.to_radians()/ 2.0f );
		tux_position += circle_center;
		tux_position.x -= tux.get_width()/2;
		tux_position.y -= tux.get_height()/2;

		// Give tux circle blue outer outline, because it looks nice
		canvas.fill_circle(tux_position.x + tux_radius, tux_position.y + tux_radius, tux_radius+tux_circle, clan::Colorf(0.0f, 0.0f, 1.0f, 1.0f));

		// Make see through border
		canvas.set_blend_state(blend_state_off);
		canvas.fill_circle(tux_position.x + tux_radius, tux_position.y + tux_radius, tux_radius+tux_circle-2, clan::Colorf(0.0f, 0.0f, 0.0f, 0.0f));
		canvas.reset_blend_state();

		// Give tux circle blue outline, to mask the alpha channel
		canvas.fill_circle(tux_position.x + tux_radius, tux_position.y + tux_radius, tux_radius+2, clan::Colorf(0.0f, 0.0f, 1.0f, 1.0f));

		// Draw tux
		tux.draw(canvas, tux_position.x, tux_position.y);

		// Draw text
		font_large.draw_text(canvas, 10-2, 50-2, "ClanLib Layered Window", clan::Colorf(0.1f, 0.1f, 0.1f, 1.0f));
		font_large.draw_text(canvas, 10, 50, "ClanLib Layered Window", clan::Colorf::green);
		font_small.draw_text(canvas, 60-2, 80-2, "Click mouse on the penguin to exit", clan::Colorf(0.1f, 0.1f, 0.1f, 1.0f));
		font_small.draw_text(canvas, 60, 80, "Click mouse on the penguin to exit", clan::Colorf::green);
		font_small.draw_text(canvas, 110-2, 110-2, "Drag rock to move window", clan::Colorf(0.1f, 0.1f, 0.1f, 1.0f));
		font_small.draw_text(canvas, 110, 110, "Drag rock to move window", clan::Colorf::green);

		canvas.flip(1);

		// This call processes user input and other events
		clan::KeepAlive::process();
	}

	return 0;
}

void App::on_mouse_down(const clan::InputEvent &key)
{
	// Check for click on penguin
	int centre_x = key.mouse_pos.x - ( tux_position.x + tux_radius);
	int centre_y = key.mouse_pos.y - ( tux_position.y + tux_radius);
	int distance = sqrt( (float) ( centre_x * centre_x + centre_y * centre_y ) );
	if (distance < tux_radius)
		quit = true;

	last_mouse_pos = key.mouse_pos;
	drag_start = true;
}

void App::on_mouse_up(const clan::InputEvent &key)
{
	drag_start = false;
}

void App::on_lost_focus()
{
	drag_start = false;
}

void App::on_mouse_move(const clan::InputEvent &key, clan::DisplayWindow *window)
{
	if (drag_start)
	{
			clan::Rect geometry = window->get_geometry();
			geometry.translate(key.mouse_pos.x - last_mouse_pos.x, key.mouse_pos.y - last_mouse_pos.y);
			window->set_position(geometry.left, geometry.top);
	}
}

void App::on_window_close(clan::DisplayWindow *window)
{
	quit = true;
}

void App::on_input_up(const clan::InputEvent &key)
{
	if(key.id == clan::keycode_escape)
	{
		quit = true;
	}
}
