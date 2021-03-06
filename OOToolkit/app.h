#pragma once
#ifndef _APP_H_
#define _APP_H_

#include "OOToolkit.h"	

// Dimensions (defined by your app)
#define FRAME_MEMSIZE   (1024 * 1024 * 192)
#define FRAME_WIDTH     (             1920)
#define FRAME_HEIGHT    (             1080)
#define FRAME_DEPTH     (                4)
#define FRAME_NUMBUF    (                2)

// font sizes etc..
#define FONT_SIZE       (               48)

// calculate where the middle of the screen is.
#define MIDDLE_X        (  FRAME_WIDTH / 2)
#define MIDDLE_Y        ( FRAME_HEIGHT / 2)

class Application {

	std::unique_ptr<OOToolkit> kit;

	std::vector<int> fonts;
	std::vector<int> sounds;
	std::vector<int> sprites;

	// being incremented every frame.
	unsigned long long counter;

	Color drawCol;

	// generated prng seed.
	uint32_t prngSeed;

public:
	Application();
	~Application();

	void Load();
	bool Frame();
	void Quit();
};

#endif /* _APP_H_ */
