#include "OOToolkit.h" /* Include the class definitions */

// OOToolkit Implementation File.
#define OOCRASH OOerrorOut(__FILE__, __FUNCTION__, __LINE__)
#define OOCRASHMSG(x) OOerrorOut(__FILE__, __FUNCTION__, __LINE__, (x))

// StbImage
//#define STBI_ASSERT(x) if (!(x)) OOCRASHMSG("STBI Assertion Failed!");
#define STBI_ASSERT(x) 
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

// OggVorbis
#include "oggvorbis/ogg.h"
#include "oggvorbis/codec.h"
#include "oggvorbis/vorbisfile.h"

// MiniMp3
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3/minimp3_ex.h"

// DrWav
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <sys/socket.h>
#include <arpa/inet.h>


#define OOAUDIOINST (UINT16_MAX)

#pragma region // OOTcpClient

OOTcpClient::OOTcpClient() {
	this->connected = false;
	this->sock = -1;
	this->myStream.clear();
}

OOTcpClient::~OOTcpClient() {
	if (this->connected) {
		close(this->sock);
		this->sock = -1;
		this->connected = false;
		this->myStream.clear();
	}
}

void OOTcpClient::Init(const std::string& host, uint16_t port, bool sendMsg) {
	int rc;
	struct sockaddr_in server_address;
	fd_set myset;
	int enable = 1;
	timeval tv;
	tv.tv_sec = 2; // connection timeout is very small so the app won't hang for a long time.
	tv.tv_usec = 0;
	timespec ts;
	ts.tv_sec = 5;
	ts.tv_nsec = 0;
	this->connected = false;

	STDOUTLOG << "Connecting...";
	
	this->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (this->sock < 0) {
		STDOUTLOG << " err " << __LINE__;
		return;
	}

	// set Keep-Alive option and receive timeout.
	setsockopt(this->sock, SOL_SOCKET, 0x0008, reinterpret_cast<void*>(&enable), sizeof(enable));
	setsockopt(this->sock, SOL_SOCKET, 0x1006, reinterpret_cast<void*>(&ts), sizeof(ts));

	// set non-blocking mode
	int flags = fcntl(this->sock, F_GETFL, 0);
	fcntl(this->sock, F_SETFL, flags | O_NONBLOCK);

	// try to parse IP address.
	in_addr parsed;
	memset(&parsed, 0, sizeof(parsed));
	rc = inet_aton(host.c_str(), &parsed);
	if (rc == 0) {
		STDOUTLOG << "Failed to parse IP Address!";
		goto cleanupsock;
	}

	// prepare server address structure.
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(port);
	server_address.sin_addr = parsed;

	// try to connect.
	rc = connect(this->sock, reinterpret_cast<const sockaddr *>(&server_address), sizeof(server_address));
	if (rc < 0) {
		if (errno == 36) {
			do {
				FD_ZERO(&myset);
				FD_SET(this->sock, &myset);
				rc = select(this->sock + 1, nullptr, &myset, nullptr, &tv);
				if (rc < 0 && errno != EINTR) {
					// unable to connect.
					STDOUTLOG << " err " << __LINE__;
					goto cleanupsock;
				}
				else if (rc > 0) {
					// socket selected for write.
					socklen_t rclen = sizeof(rc);
					if (getsockopt(this->sock, SOL_SOCKET, 0x1007, &rc, &rclen) < 0) {
						// unable to check for the error opt.
						STDOUTLOG << " err " << __LINE__;
						goto cleanupsock;
					}

					if (rc) {
						// there's some error in the connection.
						STDOUTLOG << " err " << __LINE__;
						goto cleanupsock;
					}

					// no errors, we are connected!!!
					break;
				}
				else {
					// timeout in select.
					STDOUTLOG << " err " << __LINE__;
					goto cleanupsock;
				}
			} while (true);
		}
		else {
			// error connecting.
			STDOUTLOG << " err " << __LINE__;
			goto cleanupsock;
		}
	}

	// set blocking mode again.
	flags = fcntl(this->sock, F_GETFL, 0);
	fcntl(this->sock, F_SETFL, flags & (~O_NONBLOCK));

	// send a dummy message to be SURE we're connected and can send messages, but only if we need this.
	if (sendMsg) {
		const char* msg = "Hello from OOToolkit!\n";
		size_t len = strlen(msg);
		rc = send(this->sock, msg, len, 0);
		if (rc != len) {
			goto cleanupsock;
		}
	}

	this->connected = true;
	return;

cleanupsock:
	close(this->sock);
	return;
}

template <class T> void OOTcpClient::Send(const T &v) {
	if (this->connected) {
		this->myStream << v;
	}
}

bool OOTcpClient::IsConnected() {
	return this->connected;
}

ssize_t OOTcpClient::Flush() {
	if (this->connected) {
		// get stream contents as a string.
		const std::string& str = this->myStream.str();
		size_t len = str.length();

		// send it (and clear the stream)
		ssize_t recv = send(this->sock, str.c_str(), len, 0);

		// wipe the stream.
		this->myStream.str(std::string());
		this->myStream.clear();

		return recv;
	}

	return -1;
}

std::string OOTcpClient::Receive() {
	if (this->connected) {
		ssize_t rc;
		char rbuf[MAX_TCP_PKT];
		memset(rbuf, 0, sizeof(rbuf));

		rc = recv(this->sock, rbuf, sizeof(rbuf), 0);
		if (rc < 0) {
			// we shouldn't crash here, that's fine, the server may have disconnected
			return std::string();
		}

		// specify the length, because the recv data may contain nullbytes.
		return std::string(rbuf, rc);
	}

	// okay now this is wrong.
	//OOCRASHMSG("Tried to receive data from a non-connected socket.");
	return std::string();
}

#pragma endregion

#pragma region // OOLog

OOLog::OOLog(const std::string & funcName, OOTcpClient* tcpRef) {
	this->debugLogStream << funcName << ": ";
	this->tcpClient = tcpRef;
}

OOLog::~OOLog() {
	// Append a newline (and also flush the stream).
	this->debugLogStream << std::endl;

	// get stream contents as a string.
	const std::string& str = this->debugLogStream.str();

	// either printf it or send via tcp.
	if (this->tcpClient == nullptr) {
		printf("%s", str.c_str());
	}
	else {
		this->tcpClient->Send(str);
		this->tcpClient->Flush();
	}
}

#pragma endregion

#pragma region // Misc functions

void OOerrorOut(const char* file, const char* func, int line, const char* msg) {
	DEBUGLOG << "[DEBUG] [ERROR] -- OpenOrbis Toolkit ERROR BEGIN --";
	DEBUGLOG << "File: " << file;
	DEBUGLOG << "Function: " << func;
	DEBUGLOG << "Line: " << line;
	if (msg != nullptr) {
		DEBUGLOG << "Message: " << msg;
	}
	DEBUGLOG << "[DEBUG] [ERROR] -- OpenOrbis Toolkit ERROR END --";

	abort();
	for (;;);
}

#pragma endregion

#pragma region // OOController

OOController::OOController() {
	this->pad = -1;
}

OOController::~OOController() {
	scePadClose(this->pad);
	DEBUGLOG << "[DEBUG] [CONTROLLER] OOController freed!";
}

bool OOController::Init(int controllerUserID) {
	// Initialize the Pad library
	if (scePadInit() != ORBIS_OK) {
		DEBUGLOG << "[DEBUG] [CONTROLLER] [ERROR] Failed to initialize pad library!";
		return false;
	}

	// Get the user ID
	if (controllerUserID < 0) {
		DEBUGLOG << "[DEBUG] [CONTROLLER] User ID <0 passed, autodetecting...";
		OrbisUserServiceInitializeParams param;
		param.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
		sceUserServiceInitialize(&param);
		sceUserServiceGetInitialUser(&this->userID);
	}
	else {
		this->userID = controllerUserID;
	}

	// Open a handle for the controller
	this->pad = scePadOpen(this->userID, ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);

	if (this->pad < 0) {
		DEBUGLOG << "[DEBUG] [CONTROLLER] [ERROR] Failed to open pad!";
		return false;
	}

	this->lastVib.lgMotor = 0;
	this->lastVib.smMotor = 0;
	this->padData = { NULL };

	DEBUGLOG << "[DEBUG] [CONTROLLER] Controller init ok!";

	return true;
}

int OOController::GetUserID() {
	return this->userID;
}

std::string OOController::GetUserName() {
	char username[256];
	memset(username, 0, sizeof(username));
	if (sceUserServiceGetUserName(this->userID, username, sizeof(username) - 1) < 0) {
		return "";
	}
	else {
		return username;
	}
}

void OOController::setButtonState(int state) {
	this->prevButtonState = this->buttonState;
	this->buttonState = state;
}

void OOController::UpdateState() {
	scePadReadState(this->pad, &this->padData);
	setButtonState(this->padData.buttons);
}

bool OOController::CheckButtonHeld(int stateToCheck) {
	return (stateToCheck & this->buttonState);
}

bool OOController::CheckButtonPressed(int stateToCheck) {
	return (stateToCheck & this->buttonState && !(stateToCheck & this->prevButtonState));
}

bool OOController::CheckButtonReleased(int stateToCheck) {
	return (!(stateToCheck & this->buttonState) && (stateToCheck & this->prevButtonState));
}

int OOController::SetVibration(uint8_t left, uint8_t right) {
	this->lastVib.lgMotor = left;
	this->lastVib.smMotor = right;

	return scePadSetVibration(this->pad, &this->lastVib);
}

int OOController::SetVibration(OrbisPadVibeParam param) {
	this->lastVib = param;

	return scePadSetVibration(this->pad, &this->lastVib);
}

int OOController::GetStick(bool get_right_stick, stick& out) {
	if (this->pad < 0) return 0;

	if (get_right_stick) {
		out.x = this->padData.rightStick.x;
		out.y = this->padData.rightStick.y;
		
	}
	else {
		out.x = this->padData.leftStick.x;
		out.y = this->padData.leftStick.y;
	}

	return 1;
}

OrbisPadVibeParam OOController::GetVibration() {
	return this->lastVib;
}

#pragma endregion

#pragma region // OOPNG

OOPNG::OOPNG(size_t bufsize, unsigned char* bufpng) {
	this->img = reinterpret_cast<uint32_t *>(stbi_load_from_memory(bufpng, bufsize, &this->width, &this->height, &this->channels, STBI_rgb_alpha));

	if (this->img == nullptr) {
		OOCRASHMSG("Failed to load PNG image from memory.");
		return;
	}
}

OOPNG::OOPNG(const char *imagePath) {
	this->img = reinterpret_cast<uint32_t *>(stbi_load(imagePath, &this->width, &this->height, &this->channels, STBI_rgb_alpha));

	if (this->img == nullptr) {
		OOCRASHMSG("Failed to load PNG image.");
		return;
	}
}

OOPNG::~OOPNG() {
	if (this->img != nullptr) {
		DEBUGLOG << "[DEBUG] [PNG] Freeing image...";
		stbi_image_free(this->img);
		this->img = nullptr;

		// also reset other properties just in case.
		this->width = 0;
		this->height = 0;
		this->channels = 0;
	}
}

void OOPNG::GetInfo(SpriteDim& sdim) {
	sdim.w = this->width;
	sdim.h = this->height;
	sdim.c = this->channels;
}

bool OOPNG::IsFreed() {
	return this->img == nullptr;
}

void OOPNG::DrawPart(OOScene2D& scene, int startX, int startY, int left, int top, int width, int height) {
	// Don't draw non-existant images
	if (this->img == nullptr) {
		OOCRASHMSG("Trying to draw a non-existant image!");
	}

	// error checking.
	if (width > this->width || height > this->height || width < 0 || height < 0) {
		OOCRASHMSG("Invalid width/height passed to DrawPart.");
	}

	// Iterate the bitmap and draw the pixels
	for (int yPos = top; yPos < height; yPos++) {
		for (int xPos = left; xPos < width; xPos++) {
			// Decode the 32-bit color
			uint32_t encodedColor = this->img[(yPos * this->width) + xPos];

			// Get new pixel coordinates to account for start coordinates
			int x = startX + xPos;
			int y = startY + yPos;

			// Re-encode the color
			uint8_t r = (uint8_t)(encodedColor >> 0);
			uint8_t g = (uint8_t)(encodedColor >> 8);
			uint8_t b = (uint8_t)(encodedColor >> 16);

			Color color = { r, g, b, 255 };

			scene.DrawPixel(x, y, color);
		}
	}
}

void OOPNG::Draw(OOScene2D& scene, int startX, int startY) {
	this->DrawPart(scene, startX, startY, 0, 0, this->width, this->height);
}

#pragma endregion

#pragma region // OOScene2D

OOScene2D::OOScene2D() {
	// zero-out pointers and variables.
	this->width = 0;
	this->height = 0;
	this->depth = 0;
	this->frameBufferSize = 0;
	this->activeFrameBufferIdx = 0;
	this->frameID = 0;
	this->videoMem = nullptr;
	this->videoMemSP = nullptr;
}

OOScene2D::~OOScene2D() {
	sceVideoOutClose(this->video);
	sceKernelDeleteEqueue(this->flipQueue);
	this->deallocateVideoMem();
	DEBUGLOG << "[DEBUG] [SCENE2D] Scene2D freed!";

	FT_Done_FreeType(this->ftLib);
	DEBUGLOG << "[DEBUG] [SCENE2D] FreeType freed!";
}

bool OOScene2D::Init(int w, int h, int pixelDepth, size_t memSize, int numFrameBuffers) {
	int rc;

	this->width = w;
	this->height = h;
	this->depth = pixelDepth;
	this->frameBufferSize = this->width * this->height * this->depth;

	this->video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);

	if (this->video < 0) {
		DEBUGLOG << "[DEBUG] [SCENE2D] Failed to open a video out handle: " << std::string(strerror(errno));
		return false;
	}

	// Load freetype
	rc = sceSysmoduleLoadModule(0x009A);

	if (rc != ORBIS_OK) {
		DEBUGLOG << "[DEBUG] [SCENE2D] Failed to load freetype: " << std::string(strerror(errno));
		return false;
	}

	// Initialize freetype
	rc = FT_Init_FreeType(&this->ftLib);

	if (rc != ORBIS_OK) {
		DEBUGLOG << "[DEBUG] [SCENE2D] Failed to initialize freetype: " << std::string(strerror(errno));
		return false;
	}

	if (!initFlipQueue()) {
		DEBUGLOG << "[DEBUG] [SCENE2D] Failed to initialize flip queue: " << std::string(strerror(errno));
		return false;
	}

	if (!allocateVideoMem(memSize, 0x200000)) {
		DEBUGLOG << "[DEBUG] [SCENE2D] Failed to allocate video memory: " << std::string(strerror(errno));
		return false;
	}

	if (!allocateFrameBuffers(numFrameBuffers)) {
		DEBUGLOG << "[DEBUG] [SCENE2D] Failed to allocate frame buffers: " << std::string(strerror(errno));
		return false;
	}

	sceVideoOutSetFlipRate(this->video, 0);
	return true;
}

bool OOScene2D::initFlipQueue() {
	int rc = sceKernelCreateEqueue(&flipQueue, "OOToolkit Flip Queue");

	if (rc < 0) {
		return false;
	}

	sceVideoOutAddFlipEvent(flipQueue, this->video, 0);
	return true;
}

bool OOScene2D::allocateFrameBuffers(int num) {
	// Allocate frame buffers array
	this->frameBuffers = new char*[num];

	// Set the display buffers
	for (int i = 0; i < num; i++) {
		this->frameBuffers[i] = this->allocateDisplayMem(this->frameBufferSize);
	}

	// Set SRGB pixel format
	sceVideoOutSetBufferAttribute(&this->attr, 0x80000000, 1, 0, this->width, this->height, this->width);

	// Register the buffers to the video handle
	return (sceVideoOutRegisterBuffers(this->video, 0, (void **)this->frameBuffers, num, &this->attr) == ORBIS_OK);
}

char *OOScene2D::allocateDisplayMem(size_t size) {
	// Essentially just bump allocation
	char *allocatedPtr = this->videoMemSP;
	this->videoMemSP += size;

	return allocatedPtr;
}

bool OOScene2D::allocateVideoMem(size_t size, int alignment) {
	int rc;

	// Align the allocation size
	this->directMemAllocationSize = (size + alignment - 1) / alignment * alignment;

	// Allocate memory for display buffer
	rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), this->directMemAllocationSize, alignment, 3, &this->directMemOff);

	if (rc < 0) {
		this->directMemAllocationSize = 0;
		return false;
	}

	// Map the direct memory
	rc = sceKernelMapDirectMemory(&this->videoMem, this->directMemAllocationSize, 0x33, 0, this->directMemOff, alignment);

	if (rc < 0) {
		sceKernelReleaseDirectMemory(this->directMemOff, this->directMemAllocationSize);

		this->directMemOff = 0;
		this->directMemAllocationSize = 0;

		return false;
	}

	// Set the stack pointer to the beginning of the buffer
	this->videoMemSP = reinterpret_cast<char *>(this->videoMem);

	return true;
}

void OOScene2D::deallocateVideoMem() {
	// Free the direct memory
	sceKernelReleaseDirectMemory(this->directMemOff, this->directMemAllocationSize);

	// Zero out meta data
	this->videoMem = nullptr;
	this->videoMemSP = nullptr;
	this->directMemOff = 0;
	this->directMemAllocationSize = 0;

	// Free the frame buffer array
	delete this->frameBuffers;
	this->frameBuffers = nullptr;
}

void OOScene2D::SetActiveFrameBuffer(int index) {
	this->activeFrameBufferIdx = index;
}

void OOScene2D::SubmitFlip(int frameID) {
	sceVideoOutSubmitFlip(this->video, this->activeFrameBufferIdx, ORBIS_VIDEO_OUT_FLIP_VSYNC, frameID);
}

void OOScene2D::FrameWait(int frameID) {
	OrbisKernelEvent evt;
	int count;

	// If the video handle is not initialized, bail out. This is mostly a failsafe, this should never happen.
	if (this->video == 0) {
		return;
	}

	for (;;) {
		OrbisVideoOutFlipStatus flipStatus;

		// Get the flip status and check the arg for the given frame ID
		sceVideoOutGetFlipStatus(video, &flipStatus);

		if (flipStatus.flipArg == frameID) {
			break;
		}

		// Wait on next flip event
		if (sceKernelWaitEqueue(this->flipQueue, &evt, 1, &count, 0) != ORBIS_OK) {
			break;
		}
	}
}

void OOScene2D::FrameBufferSwap() {
	// Swap the frame buffer for some perf
	this->activeFrameBufferIdx = (this->activeFrameBufferIdx + 1) % 2;
}

void OOScene2D::FrameBufferClear() {
	// Clear the screen with a black frame buffer
	this->FrameBufferFill(COLOR_BLACK);
}

int OOScene2D::InitPNG(const std::string& fname) {
	this->sprites.emplace_back(fname.c_str());
	return this->sprites.size() - 1;
}

int OOScene2D::InitPNG(size_t bufSize, unsigned char *pngBuf) {
	if (pngBuf == nullptr || bufSize == 0) {
		OOCRASHMSG("PNG buffer is null.");
	}

	this->sprites.emplace_back(bufSize, pngBuf);
	return this->sprites.size() - 1;
}

void OOScene2D::DrawPNG(int x, int y, int index) {
	if (index < 0 || index > this->sprites.size() - 1) {
		OOCRASHMSG("PNG index out of range.");
	}

	if (this->sprites[index].IsFreed()) {
		OOCRASHMSG("PNG is freed.");
	}

	this->sprites[index].Draw(*this, x, y);
}

void OOScene2D::DrawPNGPart(int x, int y, int left, int top, int width, int height, int index) {
	if (index < 0 || index > this->sprites.size() - 1) {
		OOCRASHMSG("PNG index out of range.");
	}

	if (this->sprites[index].IsFreed()) {
		OOCRASHMSG("PNG is freed.");
	}

	this->sprites[index].DrawPart(*this, x, y, left, top, width, height);
}

void OOScene2D::FreePNG(int index) {
	if (index < 0 || index > this->sprites.size() - 1) {
		OOCRASHMSG("PNG index out of range.");
	}

	if (this->sprites[index].IsFreed()) {
		OOCRASHMSG("PNG is freed.");
	}

	this->sprites[index].~OOPNG();
}

void OOScene2D::CalcSpriteDim(int sprite, SpriteDim& out) {
	if (sprite < 0 || sprite > this->sprites.size() - 1) {
		OOCRASHMSG("PNG index out of range.");
	}

	if (this->sprites[sprite].IsFreed()) {
		OOCRASHMSG("PNG is freed.");
	}

	this->sprites[sprite].GetInfo(out);
}

void OOScene2D::Commit() {
	// Submit the frame buffer
	this->SubmitFlip(this->frameID);
	this->FrameWait(this->frameID);

	// Swap to the next buffer
	this->FrameBufferSwap();
	this->frameID++;
}

bool OOScene2D::initFont(FT_Face *face, const char *fontPath, int fontSize) {
	int rc;

	rc = FT_New_Face(this->ftLib, fontPath, 0, face);

	if (rc < 0) {
		return false;
	}

	rc = FT_Set_Pixel_Sizes(*face, 0, fontSize);

	if (rc < 0) {
		return false;
	}

	return true;
}

bool OOScene2D::initMemFont(FT_Face *face, size_t bufSize, unsigned char* fontBuf, int fontSize) {
	int rc;

	rc = FT_New_Memory_Face(this->ftLib, fontBuf, bufSize, 0, face);

	if (rc < 0) {
		return false;
	}

	rc = FT_Set_Pixel_Sizes(*face, 0, fontSize);

	if (rc < 0) {
		return false;
	}

	return true;
}

int OOScene2D::InitFont(const std::string& fname, int fontSize) {
	this->fonts.push_back({ });
	this->initFont(&(this->fonts.back()), fname.c_str(), fontSize);
	return this->fonts.size() - 1;
}

int OOScene2D::InitFont(size_t bufSize, unsigned char *fontBuf, int fontSize) {
	this->fonts.push_back({ });
	this->initMemFont(&(this->fonts.back()), bufSize, fontBuf, fontSize);
	return this->fonts.size() - 1;
}

bool OOScene2D::FreeFont(int index) {
	if (index < 0 || index > this->fonts.size() - 1) {
		OOCRASHMSG("Font index out of range");
	}

	FT_Done_Face(this->fonts[index]);
	this->fonts[index] = { };

	return true;
}

void OOScene2D::FrameBufferFill(Color color) {
	this->DrawRectangle(0, 0, this->width, this->height, color);
}

void OOScene2D::DrawPixel(int x, int y, Color color) {
	// Get pixel location based on pitch
	int pixel = (y * this->width) + x;

	// Encode to 24-bit color
	uint32_t encodedColor = 0x80000000 + (color.r << 16) + (color.g << 8) + color.b;

	// Draw to the frame buffer
	((uint32_t *)this->frameBuffers[this->activeFrameBufferIdx])[pixel] = encodedColor;
}

bool OOScene2D::GetPixel(int x, int y, Color& color) {
	// Error checking.
	if (x < 0 || y < 0 || x >= this->width || y >= this->height) {
		return false;
	}

	// Get pixel location based on pitch
	int pixel = (y * this->width) + x;

	// Get pixel.
	uint32_t col = ((uint32_t *)this->frameBuffers[this->activeFrameBufferIdx])[pixel];

	// Return color.
	color.r = (col >> 16) & 0xFF;
	color.g = (col >> 8) & 0xFF;
	color.b = col & 0xFF;
	color.a = 0xFF; // no alpha.

	return true;
}

void OOScene2D::DrawRectangle(int x, int y, int w, int h, Color color) {
	int xPos, yPos;

	// Draw row-by-row, column-by-column
	for (yPos = y; yPos < y + h; yPos++) {
		for (xPos = x; xPos < x + w; xPos++) {
			this->DrawPixel(xPos, yPos, color);
		}
	}
}

void OOScene2D::DrawTextContainer(const std::string& txt, int font, int startX, int startY, int maxW, int maxH) {
	DEBUGLOG << "[DEBUG] [SCENE2D] DrawTextContainer() Function not implemented!";
}

void OOScene2D::drawText(const char *txt, FT_Face face, int startX, int startY, Color col) {
	int rc;
	int xOffset = 0;
	int yOffset = 0;

	// Get the glyph slot for bitmap and font metrics
	FT_GlyphSlot slot = face->glyph;

	// Iterate each character of the text to write to the screen
	size_t len = strlen(txt);
	for (int n = 0; n < len; n++) {
		FT_UInt glyph_index;

		// Get the glyph for the ASCII code
		glyph_index = FT_Get_Char_Index(face, txt[n]);

		// Load and render in 8-bit color
		rc = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
		if (rc) continue;

		rc = FT_Render_Glyph(slot, ft_render_mode_normal);
		if (rc) continue;

		// If we get a newline, increment the y offset, reset the x offset, and skip to the next character
		if (txt[n] == '\n') {
			xOffset = 0;
			yOffset += slot->bitmap.width * 2;
			continue;
		}

		// Parse and write the bitmap to the frame buffer
		for (int yPos = 0; yPos < slot->bitmap.rows; yPos++) {
			for (int xPos = 0; xPos < slot->bitmap.width; xPos++) {
				// Decode the 8-bit bitmap
				char pixel = slot->bitmap.buffer[(yPos * slot->bitmap.width) + xPos];

				// Get new pixel coordinates to account for the character position and baseline, as well as newlines
				int x = startX + xPos + xOffset + slot->bitmap_left;
				int y = startY + yPos + yOffset - slot->bitmap_top;

				// Linearly interpolate between the foreground and background for smoother rendering
				uint8_t r = (pixel * col.r) / 255;
				uint8_t g = (pixel * col.g) / 255;
				uint8_t b = (pixel * col.b) / 255;

				// Create new color struct with lerp'd values
				Color finalColor = { r, g, b };

				// We need to do bounds checking before commiting the pixel write due to our transformations, or we
				// could write out-of-bounds of the frame buffer
				if (x < 0 || y < 0 || x >= this->width || y >= this->height) {
					continue;
				}

				// If the pixel in the bitmap isn't blank, we'll draw it
				if (pixel != 0) {
					this->DrawPixel(x, y, finalColor);
				}
			}
		}

		// Increment x offset for the next character
		xOffset += slot->advance.x >> 6;
	}
}

void OOScene2D::DrawText(const std::string& txt, int font, int startX, int startY, Color col) {
	if (font < 0 || font > this->fonts.size() - 1) {
		OOCRASHMSG("Font index out of range.");
	}

	this->drawText(txt.c_str(), this->fonts[font], startX, startY, col);
}

int getNewLine(FT_Face face) {
	FT_UInt glyph_index = FT_Get_Char_Index(face, '\n');
	FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
	FT_Render_Glyph(face->glyph, ft_render_mode_normal);
	return (face->glyph->bitmap.width * 2);
}

void OOScene2D::calcTextDim(const char *txt, FT_Face face, TextDim& textDimm) {
	int rc;
	int xOffset = 0;
	int yOffset = 0;

	// Get the glyph slot for bitmap and font metrics
	FT_GlyphSlot slot = face->glyph;

	// Iterate each character of the text to write to the screen
	size_t len = strlen(txt);
	textDimm.h = getNewLine(face);
	int nl = textDimm.h;
	for (int n = 0; n < len; n++) {
		FT_UInt glyph_index;

		// Get the glyph for the ASCII code
		glyph_index = FT_Get_Char_Index(face, txt[n]);

		// Load and render in 8-bit color
		rc = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
		if (rc) {
			continue;
		}

		rc = FT_Render_Glyph(slot, ft_render_mode_normal);
		if (rc) {
			continue;
		}

		// If we get a newline, increment the y offset, reset the x offset, update y size, and skip to the next character
		if (txt[n] == '\n') {
			xOffset = 0;
			yOffset += slot->bitmap.width * 2; // what the hell? that makes no sense!
			textDimm.h += nl;
			continue;
		}

		// Increment x offset for the next character
		xOffset += slot->advance.x >> 6;

		// Update the x size to be the *widest* offset (in case of multiple lines that's important)
		if (textDimm.w < xOffset) {
			textDimm.w = xOffset;
		}
	}
}

void OOScene2D::CalcTextDim(const std::string& txt, int font, TextDim& textDimm) {
	if (font < 0 || font > this->fonts.size() - 1) {
		OOCRASHMSG("Invalid font index.");
	}

	this->calcTextDim(txt.c_str(), this->fonts[font], textDimm);
}

#pragma endregion

#pragma region // OOAudio

OOAudio::OOAudio() {

}

OOAudio::~OOAudio() {
	sceAudioOutClose(this->audioHandle);

	for (auto& d : this->audioSamples) {
		if (d.sampleData != nullptr) {
			delete[] d.sampleData;
		}
	}
	this->audioSamples.clear();

	for (auto& d : this->audioThreads) {
		// stop all threads?
	}

	DEBUGLOG << "[DEBUG] [AUDIO] Closed sceAudioOut!";
}

bool OOAudio::Init() {
	int rc = 0;

	// Initialize the audio out library.
	rc = sceAudioOutInit();

	if (rc != ORBIS_OK) {
		DEBUGLOG << "[DEBUG] [AUDIO] [ERROR] Failed to initialize sceAudioOut: " << rc;
		return false;
	}
	else {
		DEBUGLOG << "[DEBUG] [AUDIO] sceAudioOut init ok!";
	}

	return true;
}

int OOAudio::decodeOGGInternal(FILE* input) {
	if (input == nullptr) {
		OOCRASHMSG("Unable to open OGG file for reading!");
	}

	int current_section; // current bitstream.
	int rc;
	OggVorbis_File vf; // vorbisfile handle

	// Initialize vorbis.
	rc = ov_open_callbacks(input, &vf, nullptr, 0, OV_CALLBACKS_DEFAULT);
	if (rc < 0) {
		OOCRASHMSG("Input does not appear to be an OggVorbis file!");
	}

	// Get info about ogg.
	vorbis_info *vi = ov_info(&vf, -1);

	// Save channels and frequency info.
	int channels = vi->channels;
	long freq = vi->rate;

	// Calculate the raw size and allocate the sound buffer.
	// samples are 2 bytes long, meaning one sample is a short.
	int64_t chanTotal = ov_pcm_total(&vf, -1);
	size_t pszSample = chanTotal * channels;
	size_t pOffset = 0;
	short *pSample = new short[pszSample];
	if (pSample == nullptr) {
		OOCRASHMSG("Unable to allocate memory for the sound.");
	}

	DEBUGLOG << "[DEBUG] [AUDIO] Decoding OggVorbis file...";
	for (;;) {
		long read = ov_read(&vf, (char *)pSample + pOffset, pszSample * sizeof(short), 0, 2, 1, &current_section);
		if (read == 0) {
			DEBUGLOG << "[DEBUG] [AUDIO] OGG Decoded OK!";
			break;
		}
		else if (read < 0) {
			OOCRASHMSG("An OggVorbis error has occured when decoding the OGG.");
		}
		else {
			// You can't control how many bytes ov_read will return
			// Which is the main problem why a "decode while playing" sample is not possible
			// If ov_read returns less bytes than one sample is, you're screwed!
			// And it does so frequently!
			pOffset += read;
		}
	}

	// We're done decoding and don't need vorbis anymore.
	// OV_CALLBACKS_DEFAULT should close the file descriptor for us.
	rc = ov_clear(&vf);
	if (rc != 0) {
		OOCRASHMSG("Failed to properly close the OGG file.");
	}

	// Push the info about the sound.
	this->audioSamples.push_back({ 0, pszSample, pSample, channels });

	// Return the index
	return this->audioSamples.size() - 1;
}

int OOAudio::InitSoundOGG(const std::string& fname) {
	FILE* input = fopen(fname.c_str(), "rb");
	return this->decodeOGGInternal(input);
}

int OOAudio::InitSoundOGG(size_t bufSize, unsigned char* buf) {
	if (buf == nullptr || bufSize == 0) {
		OOCRASHMSG("Invalid buffer passed.");
	}

	FILE* meminput = fmemopen(buf, bufSize, "rb");
	return this->decodeOGGInternal(meminput);
}

int OOAudio::decodeWAVInternal(drwav& dr) {
	// Calculate the sample count and allocate a buffer for the sample data accordingly
	size_t sampleCount = dr.totalPCMFrameCount * dr.channels;
	drwav_int16 *pSampleData = new drwav_int16[sampleCount];

	// Decode the wav into pSampleData
	drwav_uint64 read = drwav_read_pcm_frames_s16(&dr, dr.totalPCMFrameCount, pSampleData);
	if (read != dr.totalPCMFrameCount) {
		DEBUGLOG << "[DEBUG] [AUDIO] [ERROR] Unable to decode all PCM samples! Only read " << read;
	}

	// Push the info about the sound to a vector.
	this->audioSamples.push_back({ 0, sampleCount, pSampleData, dr.channels });

	// Deinit drwav as we don't need it anymore.
	drwav_uninit(&dr);

	DEBUGLOG << "[DEBUG] [AUDIO] Sound init ok!";

	return this->audioSamples.size() - 1;
}

int OOAudio::InitSoundWAV(const std::string& fname) {
	int rc;
	drwav dr;
	
	rc = drwav_init_file(&dr, fname.c_str(), nullptr);
	if (!rc) {
		DEBUGLOG << "[DEBUG] [AUDIO] [ERROR] Sound init fail! " << rc;
		return -1;
	}

	return this->decodeWAVInternal(dr);
}

int OOAudio::InitSoundWAV(size_t bufSize, unsigned char* buf) {
	int rc;
	drwav dr;

	rc = drwav_init_memory(&dr, buf, bufSize, nullptr);
	if (!rc) {
		DEBUGLOG << "[DEBUG] [AUDIO] [ERROR] Sound init fail! " << rc;
		return -1;
	}

	return this->decodeWAVInternal(dr);
}

int OOAudio::decodeMP3Internal(const char *fname) {
	mp3dec_t mp3d;
	mp3dec_file_info_t info;
	DEBUGLOG << "[DEBUG] [AUDIO] Decoding an MP3 file...";
	int rc = mp3dec_load(&mp3d, fname, &info, nullptr, nullptr);

	if (rc) {
		DEBUGLOG << "[DEBUG] [AUDIO] [ERROR] Error code " << rc;
		OOCRASHMSG("Error when loading MP3 file.");
	}

	if (info.hz != 48000) {
		DEBUGLOG << "[DEBUG] [AUDIO] [ERROR] SceAudioOut only supports 48000 hz, nothing more, nothing less.";
		OOCRASHMSG("Invalid sound frequency, only 48000 hertz are supported.");
	}

	if (info.channels > 2) {
		DEBUGLOG << "[DEBUG] [AUDIO] [ERROR] Unsupported amount of channels!";
		OOCRASHMSG("Invalid channel amount.");
	}

	// reallocate the buffer because FreeSound calls delete[] on it, while mp3dec is using malloc()
	// and mixing them is an Undefined Behaviour
	mp3d_sample_t* cppbuf = new mp3d_sample_t[info.samples];
	memcpy(cppbuf, info.buffer, info.samples * sizeof(mp3d_sample_t));
	free(info.buffer);
	
	this->audioSamples.push_back({ 0, info.samples, cppbuf, info.channels });

	return this->audioSamples.size() - 1;
}

int OOAudio::InitSoundMP3(const std::string& fname) {
	int sound = this->decodeMP3Internal(fname.c_str());
	return sound;
}

int OOAudio::InitSoundMP3(size_t bufSize, unsigned char *buf) {
	if (bufSize == 0 || buf == nullptr) {
		OOCRASHMSG("Invalid buffer or buffer size.");
	}

	return -1;
	//return this->decodeMP3Internal(bufSize, buf);
}

void OOAudio::closeLog(int32_t handle) {
	DEBUGLOG << "[DEBUG] [AUDIO] Closing audio handle " << handle;
	int32_t close = sceAudioOutClose(handle);
	if (close != ORBIS_OK) {
		DEBUGLOG << "[DEBUG] [AUDIO] [ERROR] Unable to safely free the audio handle! Code: " << close;
	}
}

void OOAudio::audioPlayThread(size_t dataIndex, bool loop) {
	// not pass by reference, we want a ***copy*** of the struct
	// NOT A reference to it.
	// otherwise the playback will be screwed up.
	this->audioThreadMutex.lock();
	OOSampleData mySample = this->audioSamples[this->audioThreadData[dataIndex].mysound];
	this->audioThreadMutex.unlock();

	int par = ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO;
	if (mySample.channels == 1) {
		par = ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_MONO;
	}

	int32_t handle = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, AUDIO_GRAIN, 48000, par);

	int16_t *pSample = nullptr;
	int rc;

	for (;;) {
		rc = sceAudioOutOutput(handle, nullptr); // wait till the last sample finishes playing

		this->audioThreadMutex.lock();

		OOAudioThreadData& dat = this->audioThreadData.at(dataIndex);
		if (dat.stop || mySample.sampleOffset >= mySample.sampleCount) {

			// loop from beginning if not actually stopping.
			if (!dat.stop && loop) {
				DEBUGLOG << "[DEBUG] [AUDIO] Looping back...";
				mySample.sampleOffset = 0;
				continue;
			}

			DEBUGLOG << "[DEBUG] [AUDIO] Stopping audio thread...";
			dat.done = true;
			this->audioThreadMutex.unlock();
			this->closeLog(handle);
			break;
		}

		if (dat.pause) {
			this->audioThreadMutex.unlock();
			continue;
		}

		this->audioThreadMutex.unlock();

		// Get current sample.
		pSample = &(mySample.sampleData[mySample.sampleOffset]);

		// Output audio.
		rc = sceAudioOutOutput(handle, pSample);
		if (rc < 0) {
			DEBUGLOG << "[DEBUG] [AUDIO] Failed to play audio sample chunk: " << rc;
			break;
		}

		// Advance to the next sample.
		mySample.sampleOffset += AUDIO_GRAIN * mySample.channels;
	}
}

int OOAudio::PlaySound(int index, bool loop) {
	if (index < 0 || index > this->audioSamples.size() - 1) {
		OOCRASHMSG("Sound index out of range.");
	}

	if (this->audioSamples[index].sampleData == nullptr) {
		OOCRASHMSG("Sound is already freed.");
	}

	this->audioThreadData.push_back({ false, false, false, UINT8_MAX, index });
	this->audioThreads.push_back(std::thread(&OOAudio::audioPlayThread, this, this->audioThreadData.size() - 1, loop));

	return OOAUDIOINST + (this->audioThreads.size() - 1);
}

bool OOAudio::IsPaused(int id) {
	if (id >= OOAUDIOINST) {
		int index = id - OOAUDIOINST;
		this->audioThreadMutex.lock();
		bool ret = this->audioThreadData[index].pause && !this->audioThreadData[index].done;
		this->audioThreadMutex.unlock();
		return ret;
	}
	else {
		int i = 0;
		bool atleastOnePaused = false;

		this->audioThreadMutex.lock();
		for (auto& dat : this->audioThreadData) {
			if (dat.mysound == id) {
				bool ret = dat.pause && !dat.done; // pause, and thread not stopped.
				if (ret) {
					// okay so we have at least one paused instance.
					atleastOnePaused = true;
					break;
				}
			}
			i++;
		}
		this->audioThreadMutex.unlock();

		return atleastOnePaused;
	}
}

bool OOAudio::IsPlaying(int id) {
	if (id >= OOAUDIOINST) {
		int index = id - OOAUDIOINST;
		this->audioThreadMutex.lock();
		OOAudioThreadData& dat = this->audioThreadData[index];
		bool ret = !dat.done && !dat.pause && !dat.stop;
		this->audioThreadMutex.unlock();
		return ret;
	}
	else {
		int i = 0;
		bool atleastOnePlaying = false;

		this->audioThreadMutex.lock();
		for (auto& dat : this->audioThreadData) {
			if (dat.mysound == id && !dat.done) {
				bool ret = !dat.done && !dat.pause && !dat.stop; // not done, not stopped, and not paused.
				if (ret) {
					// okay so at least one thread is still playing the sound...
					atleastOnePlaying = true;
					break;
				}
			}
			i++;
		}
		this->audioThreadMutex.unlock();

		return atleastOnePlaying;
	}
}

void OOAudio::StopSound(int id) {
	if (id >= OOAUDIOINST) {
		int index = id - OOAUDIOINST;
		this->audioThreadMutex.lock();
		this->audioThreadData[index].stop = true;
		this->audioThreadMutex.unlock();

		if (this->audioThreads[index].joinable()) {
			this->audioThreads[index].join();
		}
	}
	else {
		int i = 0;
		this->audioThreadMutex.lock();
		for (auto& dat : this->audioThreadData) {
			if (dat.mysound == id && !dat.done) {
				dat.stop = true;
				this->audioThreadMutex.unlock();

				DEBUGLOG << "[DEBUG] [AUDIO] Stopping audio play thread...";
				if (this->audioThreads[i].joinable()) {
					this->audioThreads[i].join();
				}
			}
			i++;
		}

		this->audioThreadMutex.unlock();
	}
}

void OOAudio::StopAllSounds() {
	DEBUGLOG << "[DEBUG] [AUDIO] Going to stop ALL playing sounds!";

	this->audioThreadMutex.lock();

	int i = 0;
	for (auto& dat : this->audioThreadData) {
		if (!dat.done && !dat.stop) {
			dat.stop = true; // stop means notify the thread that it should stop itself.
			// done parameter is only set by the thread.

			if (this->audioThreads[i].joinable()) {
				this->audioThreads[i].join();
			}
		}

		i++;
	}

	this->audioThreadMutex.unlock();
}

void OOAudio::PauseSound(int id, bool pause) {
	if (id >= OOAUDIOINST) {
		int index = id - OOAUDIOINST;
		this->audioThreadMutex.lock();
		this->audioThreadData[index].pause = pause;
		this->audioThreadMutex.unlock();
	}
	else {
		int i = 0;
		this->audioThreadMutex.lock();
		for (auto& dat : this->audioThreadData) {
			if (dat.mysound == id) {
				dat.pause = pause;
				this->audioThreadMutex.unlock();
			}
			i++;
		}

		this->audioThreadMutex.unlock();
	}
}

bool OOAudio::FreeSound(int index) {
	if (index < 0 || index > this->audioSamples.size() - 1) {
		OOCRASHMSG("Sound index out of range.");
	}

	if (this->audioSamples[index].sampleData == nullptr) {
		OOCRASHMSG("Sound is already freed.");
	}

	delete[] this->audioSamples[index].sampleData;
	this->audioSamples[index].sampleData = nullptr;
	this->audioSamples[index].sampleCount = 0;
	this->audioSamples[index].channels = 0;
	this->audioSamples[index].sampleOffset = 0;

	return true;
}

#pragma endregion

#pragma region // OOToolkit

OOToolkit* g_ToolkitInstance = nullptr;

OOToolkit::OOToolkit() {
	// No buffering
	setvbuf(stdout, NULL, _IONBF, 0);
	DEBUGLOG << "[DEBUG] [TOOLKIT] OpenOrbis Toolkit by nik.";

	g_ToolkitInstance = this;
}

OOToolkit::~OOToolkit() {
	DEBUGLOG << "[DEBUG] [TOOLKIT] Destructor called!";
}

OOAudio *OOToolkit::GetAudio() {
	if (this->Audio.get() == nullptr) {
		DEBUGLOG << "[DEBUG] [TOOLKIT] Making audio...";
		this->Audio.reset(new OOAudio());
	}

	return this->Audio.get();
}

OOScene2D *OOToolkit::GetScene2D() {
	if (this->Scene2D.get() == nullptr) {
		DEBUGLOG << "[DEBUG] [TOOLKIT] Making Scene2D...";
		this->Scene2D.reset(new OOScene2D());
	}

	return this->Scene2D.get();
}

OOController *OOToolkit::GetController() {
	if (this->Controller.get() == nullptr) {
		DEBUGLOG << "[DEBUG] [TOOLKIT] Making controller...";
		this->Controller.reset(new OOController());
	}

	return this->Controller.get();
}

OOTcpClient *OOToolkit::GetTcpClient() {
	if (this->TcpClient.get() == nullptr) {
		// we must not DEBUGLOG here!
		this->TcpClient.reset(new OOTcpClient());
	}

	return this->TcpClient.get();
}

#pragma endregion