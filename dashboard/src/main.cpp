#include <iostream>
#include "app/Dashboard.h"

int main() {
	Dashboard dashboard;

	if (!dashboard.initialize()) {
		std::cout << "FATAL: Failed to initialize the Dashboard!" << std::endl;
		return 1;
	}

	dashboard.run();

	return 0;
}
