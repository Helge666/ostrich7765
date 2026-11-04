// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ostrich 7765's version: here and nowhere else.  CI reads these three lines and refuses a
// v* tag that does not match them (.github/workflows/build.yml).

#ifndef OSTRICH_VERSION_HPP_INCLUDED
#define OSTRICH_VERSION_HPP_INCLUDED

#define OSTRICH_VERSION_MAJOR 0
#define OSTRICH_VERSION_MINOR 0
#define OSTRICH_VERSION_PATCH 1

#define OSTRICH_STR2(x) #x
#define OSTRICH_STR(x)  OSTRICH_STR2(x)
#define OSTRICH_VERSION_STRING \
	OSTRICH_STR(OSTRICH_VERSION_MAJOR) "." OSTRICH_STR(OSTRICH_VERSION_MINOR) "." OSTRICH_STR(OSTRICH_VERSION_PATCH)

#endif // OSTRICH_VERSION_HPP_INCLUDED
