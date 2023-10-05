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

using namespace piscsi_interface;

class StatisticsCollector
{
	friend class PiscsiResponse;

	StatisticsCollector() = default;
	~StatisticsCollector() = default;

	vector<PbStatistics> GetStatistics() const;
};
