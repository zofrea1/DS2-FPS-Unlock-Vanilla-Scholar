#pragma once

#include "settings.h"

// Installs the framerate patches into the host EXE. Returns false if the
// process is not a supported DarkSoulsII.exe or a required pattern is missing.
bool patches_apply(const Settings& settings);
void patches_remove();
