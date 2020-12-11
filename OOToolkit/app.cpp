#include "app.h"

Application::Application() {
	DEBUGLOG << "AppConstructor";
}

Application::~Application() {
	DEBUGLOG << "AppDestructor";
}

void Application::Load() {
	DEBUGLOG << "Application::Load()!";

	// reset frame counter
	this->counter = 0;

	// initialize OOToolkit and it's subsystems.
	this->kit = std::make_unique<OOToolkit>();
	this->kit->GetController()->Init(CONTROLLER_ANY_USER);
	this->kit->GetScene2D(FRAME_WIDTH, FRAME_HEIGHT, FRAME_DEPTH)->Init(FRAME_MEMSIZE, FRAME_NUMBUF);
	this->kit->GetAudio()->Init();

	// set the drawing color.
	this->drawCol = COLOR_WHITE;

	// init prng
	unsigned int seed = static_cast<unsigned int>((time(nullptr) & UINT32_MAX));
	DEBUGLOG << "-> Seed " << seed;
	srand(seed);

	// init fonts.
	DEBUGLOG << "-> Fonts!";

	// regular font.
	this->fonts.push_back(this->kit->GetScene2D()->InitFont("/app0/assets/font.ttf", FONT_SIZE));

	// init sprites
	DEBUGLOG << "-> Sprites!";

	// cat.
	this->sprites.push_back(this->kit->GetScene2D()->InitPNG("/app0/assets/image.png"));

	// init sounds
	DEBUGLOG << "-> Sounds!";

	// [unknown] synths - AM2R Ancient Power (Metroid Prime Style)
	this->sounds.push_back(this->kit->GetAudio()->InitSoundMP3("/app0/assets/power.mp3"));

	// Sync Simple - DEMPA
	// Not included here because .wav files are too large :p
	//this->sounds.push_back(this->kit->GetAudio()->InitSoundWAV("/app0/assets/music.wav"));

	// We're done, now we need to hide the splash screen.
	int32_t ret = sceSystemServiceHideSplashScreen();
	DEBUGLOG << "-> Done! " << ret;
}

bool Application::Frame() {
	this->kit->GetController()->UpdateState();
	{
		// Step Event

		// play first sound.
		if (this->kit->GetController()->CheckButtonPressed(ORBIS_PAD_BUTTON_CROSS)) {
			this->kit->GetAudio()->PlaySound(this->sounds.front(), false);
			DEBUGLOG << "played mp3 music";
		}

		// stop sound.
		if (this->kit->GetController()->CheckButtonPressed(ORBIS_PAD_BUTTON_CIRCLE)) {
			// this will stop all instances of all sounds..
			for (auto& snd : this->sounds) {
				DEBUGLOG << "Trying to stop sound " << snd;
				this->kit->GetAudio()->StopSound(snd);
			}
		}
	}

	
	this->kit->GetScene2D()->FrameBufferClear();
	{
		// Draw Event
		std::stringstream dr{"OpenOrbis Toolkit Demo."};
		int myfont = this->fonts.front(); // use the first font.
		int catsprite = this->sprites.front(); // first sprite.
		int snd0 = this->sounds.front();

		// x and y position for the text.
		int x = 128, y = 64;

		// get sprite w/h
		SpriteDim sd;
		this->kit->GetScene2D()->CalcSpriteDim(catsprite, &sd);

		// loop the cat sprite.
		this->kit->GetScene2D()->DrawPNG(counter, MIDDLE_Y - sd.h / 2, catsprite);

		// reset our frame counter.
		if (counter > FRAME_WIDTH) {
			counter = 0;
		}

		stick ls = { 0, 0 };
		this->kit->GetController()->GetStick(false, &ls);

		// make our draw string.
		dr <<
		std::endl << "Sprite XPos: " << counter <<
		std::endl << "Is first sound playing? " << (this->kit->GetAudio()->IsPlaying(snd0) ? "Yes" : "No") <<
		std::endl << "Logged on as " << this->kit->GetController()->GetUserName() <<
		std::endl << "Your logged on user id is " << this->kit->GetController()->GetUserID() <<
		std::endl << "Left Stick X/Y " << static_cast<int>(ls.x) << "/" << static_cast<int>(ls.y) <<
		std::endl << "Have fun!" << std::endl;

		// draw text.
		this->kit->GetScene2D()->DrawText(dr.str(), myfont, x, y, this->drawCol);
	}
	this->kit->GetScene2D()->Commit();

	counter++;

	return true; // return false to break out of the loop and quit the app.
}

void Application::Quit() {
	DEBUGLOG << "Application::Quit()!";
	
	// free all resources.
	for (auto& i : this->fonts) {
		this->kit->GetScene2D()->FreeFont(i);
	}

	for (auto& i : this->sprites) {
		this->kit->GetScene2D()->FreePNG(i);
	}

	for (auto& i : this->sounds) {
		this->kit->GetAudio()->FreeSound(i);
	}
}
