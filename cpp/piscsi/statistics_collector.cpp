//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "statistics_collector.h"

statistics_map StatisticsCollector::GetStatistics() const
{
	statistics_map statistics = DiskTrack::GetStatistics();
	statistics.merge(DiskCache::GetStatistics());
	statistics.merge(Disk::GetStatistics());

	return statistics;
}
