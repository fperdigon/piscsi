//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#pragma once

#include "devices/disk.h"

class StatisticsCollector
{
	friend class PiscsiResponse;

private:

	StatisticsCollector() = default;
	~StatisticsCollector() = default;

	statistics_map GetStatistics() const;
};
