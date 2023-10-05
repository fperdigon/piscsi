//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "statistics_collector.h"

vector<PbStatistics> StatisticsCollector::GetStatistics() const
{
	auto statistics = DiskTrack::GetStatistics();
	auto s = DiskCache::GetStatistics();
	statistics.insert(statistics.end(), s.begin(), s.end());
	s = Disk::GetStatistics();
	statistics.insert(statistics.end(), s.begin(), s.end());

	return statistics;
}
