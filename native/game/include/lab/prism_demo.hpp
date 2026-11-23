#pragma once

#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

// The prism demo (GAME_SPEC §12.3): a dark room, a thin slit of white light on
// the wall behind, and a prism of flint glass the player moves and turns in
// front of it. Through the prism the slit is seen spread into its spectrum:
// each pixel's view lands on the slit only at the wavelengths the glass bends
// onto it. No chemistry: the optical pass alone, on the same renderer.
// And a diffraction grating (Stage 29), a ruled plate the player moves: held
// before the slit it shows the slit itself, dimmed, and to each side of it
// the slit's spectra, violet nearest: its orders.
namespace lab {

class PrismDemo {
public:
	PrismDemo();
	// the pointer: press on the prism or the grating and drag to move it
	void pointer(float x, float y, bool down);
	// the grating's middle put at a place (as dragging it there)
	void place_grating(float x, float y);
	// turn the prism about its upright axis
	void turn(double radians);
	const DrawList &draw();
	const OpticsScene &optics();

	// for tests: where it is and how it is turned
	float centre_x() const { return cx_; }
	float centre_y() const { return cy_; }
	double turning() const { return turn_; }
	float grating_x() const { return gx_; }
	float grating_y() const { return gy_; }
	static constexpr float GRATING_W = 260.0f, GRATING_H = 300.0f;
	// 600 lines a millimetre, a common teaching grating: its ruling's pitch, nm
	static constexpr double GRATING_PITCH_NM = 1.0e6 / 600.0;
	static constexpr float SLIT_X = 1250.0f, SLIT_W = 3.0f, SLIT_Y = 260.0f, SLIT_H = 560.0f;
	static constexpr float SIDE = 240.0f, HEIGHT = 420.0f; // the prism: 5 cm on a side, drawn 240 pixels
	static constexpr double WALL_CM = 3.2; // the wall, behind the prism's centre (its far corner is 2.9 cm back)
	static constexpr double DARK = 0.0021; // the room's radiance: 0x070707
	static constexpr double SLIT = 12.0; // the slit's: 12 times the room's white, as a lamp

private:
	void compose();
	ItemView prism_;
	OpticsScene scene_;
	DrawList list_;
	float cx_ = 0, cy_ = 540;
	double turn_ = 0.0;
	float gx_ = 1600.0f, gy_ = 800.0f; // beside the slit at first, lower right
	bool dragging_ = false, dragging_grating_ = false;
	float grab_x_ = 0, grab_y_ = 0;
};

} // namespace lab
