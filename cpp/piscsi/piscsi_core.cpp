//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Powered by XM6 TypeG Technology.
// Copyright (C) 2016-2020 GIMONS
// Copyright (C) 2020-2023 Contributors to the PiSCSI project
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "shared/config.h"
#include "shared/piscsi_util.h"
#include "shared/protobuf_util.h"
#include "shared/piscsi_exceptions.h"
#include "shared/piscsi_version.h"
#include "controllers/scsi_controller.h"
#include "devices/device_logger.h"
#include "devices/device_factory.h"
#include "devices/storage_device.h"
#include "hal/gpiobus_factory.h"
#include "hal/gpiobus.h"
#include "hal/systimer.h"
#include "piscsi/piscsi_core.h"
#include <spdlog/spdlog.h>
#include <netinet/in.h>
#include <csignal>
#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>

using namespace std;
using namespace filesystem;
using namespace piscsi_interface;
using namespace piscsi_util;
using namespace protobuf_util;
using namespace scsi_defs;

void Piscsi::Banner(span<char *> args) const
{
	cout << piscsi_util::Banner("(Backend Service)");
	cout << "Connection type: " << CONNECT_DESC << '\n' << flush;

	if ((args.size() > 1 && strcmp(args[1], "-h") == 0) || (args.size() > 1 && strcmp(args[1], "--help") == 0)){
		cout << "\nUsage: " << args[0] << " [-idID[:LUN] FILE] ...\n\n";
		cout << " ID is SCSI device ID (0-7).\n";
		cout << " LUN is the optional logical unit (0-31).\n";
		cout << " FILE is a disk image file, \"daynaport\", \"bridge\", \"printer\" or \"services\".\n\n";
		cout << " Image type is detected based on file extension if no explicit type is specified.\n";
		cout << "  hd1 : SCSI-1 HD image (Non-removable generic SCSI-1 HD image)\n";
		cout << "  hds : SCSI HD image (Non-removable generic SCSI HD image)\n";
		cout << "  hdr : SCSI HD image (Removable generic HD image)\n";
		cout << "  hda : SCSI HD image (Apple compatible image)\n";
		cout << "  hdn : SCSI HD image (NEC compatible image)\n";
		cout << "  hdi : SCSI HD image (Anex86 HD image)\n";
		cout << "  nhd : SCSI HD image (T98Next HD image)\n";
		cout << "  mos : SCSI MO image (MO image)\n";
		cout << "  iso : SCSI CD image (ISO 9660 image)\n";
		cout << "  is1 : SCSI CD image (ISO 9660 image, SCSI-1)\n" << flush;

		exit(EXIT_SUCCESS);
	}
}

bool Piscsi::InitBus()
{
	bus = GPIOBUS_Factory::Create(BUS::mode_e::TARGET);
	if (bus == nullptr) {
		return false;
	}

	executor = make_unique<PiscsiExecutor>(piscsi_image, *bus, controller_manager);

	return true;
}

void Piscsi::Cleanup()
{
	if (service.IsRunning()) {
		service.Stop();
	}

	controller_manager.FlushCaches();

	executor->DetachAll();

	bus->Cleanup();
}

void Piscsi::ReadAccessToken(const path& filename)
{
	if (error_code error; !is_regular_file(filename, error)) {
		throw parser_exception("Access token file '" + filename.string() + "' must be a regular file");
	}

	if (struct stat st; stat(filename.c_str(), &st) || st.st_uid || st.st_gid) {
		throw parser_exception("Access token file '" + filename.string() + "' must be owned by root");
	}

	if (const auto perms = filesystem::status(filename).permissions();
		(perms & perms::group_read) != perms::none || (perms & perms::others_read) != perms::none ||
			(perms & perms::group_write) != perms::none || (perms & perms::others_write) != perms::none) {
		throw parser_exception("Access token file '" + filename.string() + "' must be readable by root only");
	}

	ifstream token_file(filename);
	if (token_file.fail()) {
		throw parser_exception("Can't open access token file '" + filename.string() + "'");
	}

	getline(token_file, access_token);
	if (token_file.fail()) {
		throw parser_exception("Can't read access token file '" + filename.string() + "'");
	}

	if (access_token.empty()) {
		throw parser_exception("Access token file '" + filename.string() + "' must not be empty");
	}
}

void Piscsi::LogDevices(string_view devices) const
{
	stringstream ss(devices.data());
	string line;

	while (getline(ss, line, '\n')) {
		spdlog::info(line);
	}
}

void Piscsi::TerminationHandler(int)
{
	instance->Cleanup();

	// Process will terminate automatically
}

pair<string, string> Piscsi::ParseArguments(PbCommand& command, span<char *> args, int& port)
{
	string log_level = "info";
	PbDeviceType type = UNDEFINED;
	int block_size = 0;
	string name;
	string id_and_lun;
	string reserved_ids;

	const char *locale = setlocale(LC_MESSAGES, "");
	if (locale == nullptr || !strcmp(locale, "C")) {
		locale = "en";
	}

	opterr = 1;
	int opt;
	while ((opt = getopt(static_cast<int>(args.size()), args.data(), "-Iib:d:n:p:r:t:z:D:F:L:P:R:C:v")) != -1) {
		switch (opt) {
			// The two options below are kind of a compound option with two letters
			case 'i':
			case 'I':
				continue;

			case 'd':
			case 'D':
				id_and_lun = optarg;
				continue;

			case 'b':
				if (!GetAsUnsignedInt(optarg, block_size)) {
					throw parser_exception("Invalid block size " + string(optarg));
				}
				continue;

			case 'z':
				locale = optarg;
				continue;

			case 'F':
				if (const string error = piscsi_image.SetDefaultFolder(optarg); !error.empty()) {
					throw parser_exception(error);
				}
				continue;

			case 'L':
				log_level = optarg;
				continue;

			case 'R':
				int depth;
				if (!GetAsUnsignedInt(optarg, depth)) {
					throw parser_exception("Invalid image file scan depth " + string(optarg));
				}
				piscsi_image.SetDepth(depth);
				continue;

			case 'n':
				name = optarg;
				continue;

			case 'p':
				if (!GetAsUnsignedInt(optarg, port) || port <= 0 || port > 65535) {
					throw parser_exception("Invalid port " + string(optarg) + ", port must be between 1 and 65535");
				}
				continue;

			case 'P':
				ReadAccessToken(optarg);
				continue;

			case 'v':
				cout << piscsi_get_version_string() << '\n';
				exit(EXIT_SUCCESS);

			case 'r':
				reserved_ids = optarg;
				continue;

			case 't':
				type = Device::ParseDeviceType(optarg);
				continue;

			case 1:
				// Encountered filename
				break;

			default:
				throw parser_exception("Parser error");
		}

		if (optopt) {
			throw parser_exception("Parser error");
		}

		// Set up the device data

		auto device = command.add_devices();

		if (!id_and_lun.empty()) {
			if (const string error = SetIdAndLun(*device, id_and_lun, ScsiController::LUN_MAX); !error.empty()) {
				throw parser_exception(error);
			}
		}

		device->set_type(type);
		device->set_block_size(block_size);

		ParseParameters(*device, optarg);

		SetProductData(*device, name);

		type = UNDEFINED;
		block_size = 0;
		name = "";
		id_and_lun = "";
	}

	SetLogLevel(log_level);

	return { locale, reserved_ids };
}

bool Piscsi::SetLogLevel(const string& log_level) const
{
	int id = -1;
	int lun = -1;
	string level = log_level;

	if (size_t separator_pos = log_level.find(COMPONENT_SEPARATOR); separator_pos != string::npos) {
		level = log_level.substr(0, separator_pos);

		const string l = log_level.substr(separator_pos + 1);
		separator_pos = l.find(COMPONENT_SEPARATOR);
		if (separator_pos != string::npos) {
			const string error = ProcessId(l, ScsiController::LUN_MAX, id, lun);
			if (!error.empty()) {
				spdlog::warn("Invalid device ID/LUN specifier '" + l + "'");
				return false;
			}
		}
		else if (!GetAsUnsignedInt(l, id)) {
			spdlog::warn("Invalid device ID specifier '" + l + "'");
			return false;
		}
	}

	const level::level_enum l = level::from_str(level);
	// Compensate for spdlog using 'off' for unknown levels
	if (to_string_view(l) != level) {
		spdlog::warn("Invalid log level '" + level + "'");
		return false;
	}

	set_level(l);
	DeviceLogger::SetLogIdAndLun(id, lun);

	if (id != -1) {
		if (lun == -1) {
			spdlog::info("Set log level for device ID " + to_string(id) + " to '" + level + "'");
		}
		else {
			spdlog::info("Set log level for device ID " + to_string(id) + ", LUN " + to_string(lun) + " to '" + level + "'");
		}
	}
	else {
		spdlog::info("Set log level to '" + level + "'");
	}

	return true;
}

bool Piscsi::ExecuteCommand(const CommandContext& context)
{
	const PbCommand& command = context.GetCommand();

	if (!access_token.empty() && access_token != GetParam(command, "token")) {
		return context.ReturnLocalizedError(LocalizationKey::ERROR_AUTHENTICATION, UNAUTHORIZED);
	}

	if (!PbOperation_IsValid(command.operation())) {
		spdlog::error("Received unknown command with operation opcode " + to_string(command.operation()));

		return context.ReturnLocalizedError(LocalizationKey::ERROR_OPERATION, UNKNOWN_OPERATION);
	}

	spdlog::trace("Received " + PbOperation_Name(command.operation()) + " command");

	PbResult result;

	switch(command.operation()) {
		case LOG_LEVEL:
			if (const string log_level = GetParam(command, "level"); !SetLogLevel(log_level)) {
				context.ReturnLocalizedError(LocalizationKey::ERROR_LOG_LEVEL, log_level);
			}
			else {
				context.ReturnSuccessStatus();
			}
			break;

		case DEFAULT_FOLDER:
			if (const string error = piscsi_image.SetDefaultFolder(GetParam(command, "folder")); !error.empty()) {
				context.ReturnErrorStatus(error);
			}
			else {
				context.ReturnSuccessStatus();
			}
			break;

		case DEVICES_INFO:
			piscsi_response.GetDevicesInfo(controller_manager.GetAllDevices(), result, command,
					piscsi_image.GetDefaultFolder());
			context.WriteResult(result);
			break;

		case DEVICE_TYPES_INFO:
			result.set_allocated_device_types_info(piscsi_response.GetDeviceTypesInfo(result).release());
			context.WriteResult(result);
			break;

		case SERVER_INFO:
			result.set_allocated_server_info(piscsi_response.GetServerInfo(controller_manager.GetAllDevices(),
					result, executor->GetReservedIds(), piscsi_image.GetDefaultFolder(),
					GetParam(command, "folder_pattern"), GetParam(command, "file_pattern"),
					piscsi_image.GetDepth()).release());
			context.WriteResult(result);
			break;

		case VERSION_INFO:
			result.set_allocated_version_info(piscsi_response.GetVersionInfo(result).release());
			context.WriteResult(result);
			break;

		case LOG_LEVEL_INFO:
			result.set_allocated_log_level_info(piscsi_response.GetLogLevelInfo(result).release());
			context.WriteResult(result);
			break;

		case DEFAULT_IMAGE_FILES_INFO:
			result.set_allocated_image_files_info(piscsi_response.GetAvailableImages(result,
					piscsi_image.GetDefaultFolder(), GetParam(command, "folder_pattern"),
					GetParam(command, "file_pattern"), piscsi_image.GetDepth()).release());
			context.WriteResult(result);
			break;

		case IMAGE_FILE_INFO:
			if (string filename = GetParam(command, "file"); filename.empty()) {
				context.ReturnLocalizedError( LocalizationKey::ERROR_MISSING_FILENAME);
			}
			else {
				auto image_file = make_unique<PbImageFile>();
				const bool status = piscsi_response.GetImageFile(*image_file.get(), piscsi_image.GetDefaultFolder(), filename);
				if (status) {
					result.set_status(true);
					result.set_allocated_image_file_info(image_file.get());
					context.WriteResult(result);
				}
				else {
					context.ReturnLocalizedError(LocalizationKey::ERROR_IMAGE_FILE_INFO);
				}
			}
			break;

		case NETWORK_INTERFACES_INFO:
			result.set_allocated_network_interfaces_info(piscsi_response.GetNetworkInterfacesInfo(result).release());
			context.WriteResult(result);
			break;

		case MAPPING_INFO:
			result.set_allocated_mapping_info(piscsi_response.GetMappingInfo(result).release());
			context.WriteResult(result);
			break;

		case OPERATION_INFO:
			result.set_allocated_operation_info(piscsi_response.GetOperationInfo(result,
					piscsi_image.GetDepth()).release());
			context.WriteResult(result);
			break;

		case RESERVED_IDS_INFO:
			result.set_allocated_reserved_ids_info(piscsi_response.GetReservedIds(result,
					executor->GetReservedIds()).release());
			context.WriteResult(result);
			break;

		case SHUT_DOWN:
			if (executor->ShutDown(context, GetParam(command, "mode"))) {
				TerminationHandler(0);
			}
			break;

		case NO_OPERATION:
			context.ReturnSuccessStatus();
			break;

		// TODO The image operations below can most likely directly be executed without calling the executor,
		// because they do not require the target to be idle
		case CREATE_IMAGE:
		case DELETE_IMAGE:
		case RENAME_IMAGE:
		case COPY_IMAGE:
		case PROTECT_IMAGE:
		case UNPROTECT_IMAGE:
		case RESERVE_IDS:
			return executor->ProcessCmd(context);

		// The remaining commands can only be executed when the target is idle
		// TODO What happens when the target becomes active while the command is still being executed?
		// A field 'mutex locker' can probably avoid SCSI commands and ProcessCmd() being executed at the same time
		default: {
			// TODO Find a better way to wait
			const timespec ts = { .tv_sec = 0, .tv_nsec = 500'000'000};
			while (target_is_active) {
				nanosleep(&ts, nullptr);
			}
			return executor->ProcessCmd(context);
		}
	}

	return true;
}

int Piscsi::run(span<char *> args)
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	Banner(args);

	string locale;
	string reserved_ids;
	PbCommand command;
	int port = DEFAULT_PORT;
	try {
		const auto [l, r] = ParseArguments(command, args, port);
		locale = l;
		reserved_ids = r;
	}
	catch(const parser_exception& e) {
		cerr << "Error: " << e.what() << endl;

		return EXIT_FAILURE;
	}

	if (!InitBus()) {
		cerr << "Error: Can't initialize bus" << endl;

		return EXIT_FAILURE;
	}

	if (const string error = service.Init([this] (const CommandContext& context) { return ExecuteCommand(context); }, port);
		!error.empty()) {
		cerr << "Error: " << error << endl;

		Cleanup();

		return EXIT_FAILURE;
	}

	if (command.devices_size()) {
		// Attach all specified devices
		command.set_operation(ATTACH);

		if (const CommandContext context(command, locale); !executor->ProcessCmd(context)) {
			cerr << "Error: Can't attach initial devices" << endl;

			Cleanup();

			return EXIT_FAILURE;
		}
	}

	// Display and log the device list
	PbServerInfo server_info;
	piscsi_response.GetDevices(controller_manager.GetAllDevices(), server_info, piscsi_image.GetDefaultFolder());
	const vector<PbDevice>& devices = { server_info.devices_info().devices().begin(), server_info.devices_info().devices().end() };
	const string device_list = ListDevices(devices);
	LogDevices(device_list);
	cout << device_list << flush;

	instance = this;
	// Signal handler to detach all devices on a KILL or TERM signal
	struct sigaction termination_handler;
	termination_handler.sa_handler = TerminationHandler;
	sigemptyset(&termination_handler.sa_mask);
	termination_handler.sa_flags = 0;
	sigaction(SIGINT, &termination_handler, nullptr);
	sigaction(SIGTERM, &termination_handler, nullptr);
	signal(SIGPIPE, SIG_IGN);

    // Set the affinity to a specific processor core
	FixCpu(3);

	service.Start();

	Process();

	return EXIT_SUCCESS;
}

void Piscsi::Process()
{
#ifdef USE_SEL_EVENT_ENABLE
	// Scheduling policy setting (highest priority)
	// TODO Check whether this results in any performance gain
	sched_param schparam;
	schparam.sched_priority = sched_get_priority_max(SCHED_FIFO);
	sched_setscheduler(0, SCHED_FIFO, &schparam);
#else
	cout << "Note: No PiSCSI hardware support, only client interface calls are supported" << endl;
#endif

	// Main Loop
	while (service.IsRunning()) {
#ifdef USE_SEL_EVENT_ENABLE
		// SEL signal polling
		if (!bus->PollSelectEvent()) {
			// Stop on interrupt
			if (errno == EINTR) {
				break;
			}
			continue;
		}

		// Get the bus
		bus->Acquire();
#else
		bus->Acquire();
		if (!bus->GetSEL()) {
			const timespec ts = { .tv_sec = 0, .tv_nsec = 0};
			nanosleep(&ts, nullptr);
			continue;
		}
#endif

		// Only process the SCSI command if the bus is not busy and no other device responded
		if (IsNotBusy() && bus->GetSEL()) {
			target_is_active = true;

			// Process command on the responsible controller based on the current initiator and target ID
			if (const auto shutdown_mode = controller_manager.ProcessOnController(bus->GetDAT());
				shutdown_mode != AbstractController::piscsi_shutdown_mode::NONE) {
				// When the bus is free PiSCSI or the Pi may be shut down.
				ShutDown(shutdown_mode);
			}

			target_is_active = false;
		}
	}
}

void Piscsi::ShutDown(AbstractController::piscsi_shutdown_mode shutdown_mode)
{
	Cleanup();

	switch(shutdown_mode) {
	case AbstractController::piscsi_shutdown_mode::STOP_PISCSI:
		spdlog::info("PiSCSI shutdown requested");
		break;

	case AbstractController::piscsi_shutdown_mode::STOP_PI:
		spdlog::info("Raspberry Pi shutdown requested");
		if (system("init 0") == -1) {
			spdlog::error("Raspberry Pi shutdown failed");
		}
		break;

	case AbstractController::piscsi_shutdown_mode::RESTART_PI:
		spdlog::info("Raspberry Pi restart requested");
		if (system("init 6") == -1) {
			spdlog::error("Raspberry Pi restart failed");
		}
		break;

	case AbstractController::piscsi_shutdown_mode::NONE:
		assert(false);
		break;
	}
}

bool Piscsi::IsNotBusy() const
{
    // Wait until BSY is released as there is a possibility for the
	// initiator to assert it while setting the ID (for up to 3 seconds)
	if (bus->GetBSY()) {
		const uint32_t now = SysTimer::GetTimerLow();

		// Wait for 3s
		while ((SysTimer::GetTimerLow() - now) < 3'000'000) {
			bus->Acquire();

			if (!bus->GetBSY()) {
				return true;
			}
		}

		return false;
	}

	return true;
}
