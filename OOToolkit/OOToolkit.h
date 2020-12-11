#pragma once
#ifndef _OOTOOLKIT_H_
#define _OOTOOLKIT_H_

// OOToolkit Header File.
#include <sstream>
#include <iostream>
#include <stdint.h>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>

// FreeType
#include <proto-include.h>

#include "dr_wav.h"

// PS4 specific stuff.
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/Sysmodule.h>
#include <orbis/AudioOut.h>
#include <orbis/SystemService.h>

#define GRAPHICS_USES_FONT
#define DEBUGLOG OOLog(__FUNCTION__)
#define CONTROLLER_ANY_USER (-1)

#define AUDIO_GRAIN (256)

// color defines.
#define COLOR_WHITE { 255, 255, 255, 255 }
#define COLOR_BLACK { 0,   0,   0,   255 }
#define COLOR_RED   { 255, 0, 0, 255 }
#define COLOR_GREEN { 0, 255, 0, 255 }
#define COLOR_BLUE  { 0, 0, 255, 255 }

// Never call this function, it's called by OOToolkit automatically when an error occurs.
void OOerrorOut(const char* file, const char* func, int line, const char* msg = nullptr);

// Color is used to pack together RGBA information, and is used for every function that draws colored pixels.
struct Color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a; // no alpha yet.
};

struct TextDim {
	int w; // width
	int h; // height
};

struct SpriteDim {
	int w; // width
	int h; // height
	int c; // channels
};

class OOLog {
	std::stringstream debugLogStream;
public:
	OOLog(const std::string & funcName);
	~OOLog();

	template <class T> OOLog& operator<<(const T &v) {
		this->debugLogStream << v;
		return *this;
	}
};

class OOController {
	int pad;
	int userID;
	int prevButtonState;
	int buttonState;
	OrbisPadData padData;
	OrbisPadVibeParam lastVib;

	void setButtonState(int state);

public:
	OOController();
	~OOController();

	bool Init(int controllerUserID);
	int GetUserID();

	void UpdateState();
	bool CheckButtonHeld(int stateToCheck);
	bool CheckButtonPressed(int stateToCheck);
	bool CheckButtonReleased(int stateToCheck);

	int SetVibration(uint8_t left, uint8_t right);
	int SetVibration(OrbisPadVibeParam param);
	OrbisPadVibeParam GetVibration();

	int GetStick(bool get_right_stick, stick* out);

	std::string GetUserName();
};

class OOScene2D; // cyclic dependency, OOPNG wants OOScene2D which is dependant on OOPNG.

class OOPNG {
	int width;
	int height;
	int channels;
	uint32_t *img;

public:
	OOPNG(const char *imagePath);
	OOPNG(size_t bufsize, unsigned char* bufpng);
	~OOPNG();

	bool IsFreed();
	void Draw(OOScene2D *scene, int startX, int startY);
	void GetInfo(SpriteDim* out);
};

class OOScene2D {
	FT_Library ftLib;
	std::vector<FT_Face> fonts;
	std::vector<OOPNG> sprites;

	int width;
	int height;
	int depth;
	int video;

	off_t directMemOff;
	size_t directMemAllocationSize;

	char* videoMemSP;
	void *videoMem;

	char **frameBuffers;
	OrbisKernelEqueue flipQueue;
	OrbisVideoOutBufferAttribute attr;

	int frameBufferSize;
	int frameBufferCount;
	int frameID;

	int activeFrameBufferIdx;

	bool initFlipQueue();
	bool allocateFrameBuffers(int num);
	char *allocateDisplayMem(size_t size);
	bool allocateVideoMem(size_t size, int alignment);
	void deallocateVideoMem();

	bool initFont(FT_Face *face, const char *fontPath, int fontSize);
	bool initMemFont(FT_Face *face, size_t bufSize, unsigned char* fontBuf, int fontSize);
	void drawText(const char *txt, FT_Face face, int startX, int startY, Color col);
	void calcTextDim(const char *txt, FT_Face face, TextDim *textDimm);

public:
	OOScene2D(int w, int h, int pixelDepth);
	~OOScene2D();

	bool Init(size_t memSize, int numFrameBuffers);

	void SetActiveFrameBuffer(int index);
	void SubmitFlip(int frameID);

	void FrameWait(int frameID);
	void FrameBufferSwap();
	void FrameBufferClear();
	void FrameBufferFill(Color color);

	void DrawPixel(int x, int y, Color color);
	void DrawRectangle(int x, int y, int w, int h, Color color);
	void DrawPNG(int x, int y, int index);

	bool GetPixel(int x, int y, Color* out);

	int InitPNG(const std::string& fname);
	int InitPNG(size_t bufSize, unsigned char *pngBuf);
	void FreePNG(int index);
	void CalcSpriteDim(int sprite, SpriteDim *out);

	void Commit();

	int InitFont(const std::string& fname, int fontSize);
	int InitFont(size_t bufSize, unsigned char *fontBuf, int fontSize);
	bool FreeFont(int index);
	void DrawText(const std::string& txt, int font, int startX, int startY, Color col);
	void CalcTextDim(const std::string& txt, int font, TextDim *textDimm);
	void DrawTextContainer(char *txt, FT_Face face, int startX, int startY, int maxW, int maxH);
};

struct OOSampleData {
	size_t sampleOffset;
	size_t sampleCount;
	int16_t *sampleData;
	int channels;
};

struct OOAudioThreadData {
	bool pause; // should the thread pause?
	bool stop; // should the thread stop itself?
	bool done; // the thread had finished and the sound will no longer be playing.
	uint8_t volume; // 0 - total silent, 255 - full volume.
	int mysound; // sound file index.
};

class OOAudio {
	int32_t audioHandle;
	std::vector<OOSampleData> audioSamples;

	std::mutex audioThreadMutex;
	std::vector<std::thread> audioThreads; // instances
	std::vector<OOAudioThreadData> audioThreadData;

	void closeLog(int32_t handle);

	int decodeOGGInternal(FILE *input);
	int decodeWAVInternal(drwav& dr);
	int decodeMP3Internal(const char* fname);
	void audioPlayThread(size_t dataIndex, bool loop);

public:
	OOAudio();
	~OOAudio();

	bool Init();

	// Sound init funcs for WAV and OGG.
	int InitSoundWAV(const std::string& fname);
	int InitSoundWAV(size_t bufSize, unsigned char* buf);

	int InitSoundOGG(const std::string& fname);
	int InitSoundOGG(size_t bufSize, unsigned char* buf);

	int InitSoundMP3(const std::string& fname);
	int InitSoundMP3(size_t bufSize, unsigned char* buf);

	bool FreeSound(int index);
	void StopSound(int sound_or_inst);
	void PauseSound(int index, bool pause_or_not);
	bool IsPaused(int index);
	bool IsPlaying(int index);
	int PlaySound(int index, bool loop);
};

class OOToolkit {
private:
	std::unique_ptr<OOAudio> Audio;
	std::unique_ptr<OOScene2D> Scene2D;
	std::unique_ptr<OOController> Controller;

public:

	OOToolkit();
	~OOToolkit();

	OOAudio *GetAudio();
	OOScene2D *GetScene2D(int sceneWidth = 0, int sceneHeight = 0, int pixelDepth = 4); // 4 - RGBA
	OOController *GetController();
};

#endif /* _OOTOOLKIT_H_ */
