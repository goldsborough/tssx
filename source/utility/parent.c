#include "utility/parent.h"
#include "utility/arguments.h"
#include "utility/process.h"
#include "utility/signals.h"

void setup_parent(char* name, int argc, char* argv[]) {
	if (check_flag("help", argc, argv)) {
		print_usage();
	}
	setup_parent_signals();
	start_children(name, argc, argv);
}
