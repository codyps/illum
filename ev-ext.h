#ifndef ILLUM_EV_EXT_H_
#define ILLUM_EV_EXT_H_
#pragma once

#include <ev.h>

#if EV_MULTIPLICITY
# define EV_P__  , EV_P
# define EV_A__  , EV_A
#else
# define EV_P__
# define EV_A__
#endif

#endif
