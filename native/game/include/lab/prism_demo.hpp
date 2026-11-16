#pragma once

#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

// The prism demo (GAME_SPEC §12.3): a dark room, a thin slit of white light on
// the wall behind, and a prism of flint glass the player moves and turns in
// front of it. Through the prism the slit is seen spread into its spectrum:
// each pixel's view lands on the slit only at the wavelengths the glass bends
// onto it. No chemistry: the optical pass alone, on the same renderer.
namespace lab {

class PrismDemo {
public:
	PrismDemo();
	// the pointer: press on the prism and drag to move it
	void pointer(float x, float y, bool down);
	// turn the prism about its upright axis
	void turn(double radians);
	const DrawList &draw();
	const OpticsScene &optics();

	// for tests: where it is and how it is turned
	float centre_x() const { return cx_; }
	float centre_y() const { return cy_; }
	double turning() const { return turn_; }
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
	bool dragging_ = false;
	float grab_x_ = 0, grab_y_ = 0;
};

} // namespace lab
