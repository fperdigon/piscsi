//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022-2023 Uwe Seimet
//
// Shared code for SCSI command implementations
//
//---------------------------------------------------------------------------

#pragma once

#include "shared/scsi.h"
#include <span>
#include <vector>
#include <map>

using namespace std;

class DeviceLogger;

namespace scsi_command_util
{
	void ModeSelect(const DeviceLogger&, scsi_defs::scsi_command, const vector<int>&, const vector<uint8_t>&, int, int);
	void EnrichFormatPage(map<int, vector<byte>>&, bool, int);
	void AddAppleVendorModePage(map<int, vector<byte>>&, bool);

	int GetInt16(span <const uint8_t>, int);
	int GetInt16(span <const int>, int);
	int GetInt24(span<const int>, int);
	uint32_t GetInt32(span <const int>, int);
	uint64_t GetInt64(span<const int>, int);
	void SetInt16(vector<byte>&, int, int);
	void SetInt32(vector<byte>&, int, uint32_t);
	void SetInt16(vector<uint8_t>&, int, int);
	void SetInt32(vector<uint8_t>&, int, uint32_t);
	void SetInt64(vector<uint8_t>&, int, uint64_t);
}
