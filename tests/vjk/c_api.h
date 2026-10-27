// The C code of the game the tests link (q_shared.c, bg_*.c, shared/update), with C names. q_platform.h includes
// <cstddef> when it is read as C++: it is here first, so that include finds it done inside the extern "C".
#pragma once

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>

extern "C" {
#include "qcommon/q_shared.h"
#include "update/upd_manifest.h"
#include "web/web_missing.h"
#include "game/voice/bg_voice.h"
#include "game/auth/bg_auth.h"
#include "game/boombox/bg_boombox.h"
#include "game/boombox/bg_boombox_core.h"
}

// what the stub of Com_Error throws (stubs.cpp)
struct ComError : std::runtime_error {
	int level;
	ComError( int l, const std::string &text ) : std::runtime_error( text ), level( l ) {}
};
