//---------------------------------------------------------------------------
//
//	SCSI Target Emulator PiSCSI
//	for Raspberry Pi
//
//	Powered by XM6 TypeG Technology.
//	Copyright (C) 2016-2020 GIMONS
//  Copyright (C) 2020 akuker
//
//---------------------------------------------------------------------------

// The legacy code in this file is deprecated and can cause a buffer overflow. Use spdlog directly instead.

#pragma once

#include <spdlog/spdlog.h>

static const int LOGBUF_SIZE = 512;

#define SPDLOGWRAPPER(loglevel, ...)			\
{							\
	char logbuf[LOGBUF_SIZE];				\
	snprintf(logbuf, sizeof(logbuf), __VA_ARGS__);	\
	spdlog::log(loglevel, logbuf);			\
};

#define LOGTRACE(...) SPDLOGWRAPPER(spdlog::level::trace, __VA_ARGS__)
#define LOGDEBUG(...) SPDLOGWRAPPER(spdlog::level::debug, __VA_ARGS__)
#define LOGINFO(...) SPDLOGWRAPPER(spdlog::level::info, __VA_ARGS__)
#define LOGWARN(...) SPDLOGWRAPPER(spdlog::level::warn, __VA_ARGS__)
#define LOGERROR(...) SPDLOGWRAPPER(spdlog::level::err, __VA_ARGS__)
