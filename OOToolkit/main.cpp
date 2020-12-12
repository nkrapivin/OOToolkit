#include "app.h"

int OOmain() {
	// make the application class (this will load the app).
	Application *app = new Application();

	// main loop.
	for (;;) {
		if (!app->Frame()) break;
	}

	// return from main is an abnormal behaviour on a PS4.
	delete app;
	return EXIT_FAILURE;
}

int main(void) {
    return OOmain();
}
