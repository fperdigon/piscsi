//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022-2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "mocks.h"
#include "devices/device_factory.h"
#include "controllers/controller_manager.h"

TEST(ControllerManagerTest, LifeCycle)
{
	const int ID = 4;
	const int LUN1 = 0;
	const int LUN2 = 3;

	auto bus = make_shared<MockBus>();
	ControllerManager controller_manager;
	DeviceFactory device_factory;

	auto device = device_factory.CreateDevice(SCHS, -1, "");
	EXPECT_FALSE(controller_manager.AttachToController(*bus, ID, device));

	device = device_factory.CreateDevice(SCHS, LUN1, "");
	EXPECT_TRUE(controller_manager.AttachToController(*bus, ID, device));
	EXPECT_TRUE(controller_manager.HasController(ID));
	auto controller = controller_manager.FindController(ID);
	EXPECT_NE(nullptr, controller);
	EXPECT_EQ(1, controller->GetLunCount());
	EXPECT_FALSE(controller_manager.HasController(0));
	EXPECT_EQ(nullptr, controller_manager.FindController(0));
	EXPECT_NE(nullptr, controller_manager.GetDeviceByIdAndLun(ID, LUN1));
	EXPECT_EQ(nullptr, controller_manager.GetDeviceByIdAndLun(0, 0));

	device = device_factory.CreateDevice(SCHS, LUN2, "");
	EXPECT_TRUE(controller_manager.AttachToController(*bus, ID, device));
	EXPECT_TRUE(controller_manager.HasController(ID));
	controller = controller_manager.FindController(ID);
	EXPECT_NE(nullptr, controller_manager.FindController(ID));
	EXPECT_TRUE(controller_manager.DeleteController(controller));
	EXPECT_EQ(nullptr, controller_manager.FindController(ID));

	controller_manager.DeleteAllControllers();
	EXPECT_FALSE(controller_manager.HasController(ID));
	EXPECT_EQ(nullptr, controller_manager.FindController(ID));
	EXPECT_EQ(nullptr, controller_manager.GetDeviceByIdAndLun(ID, LUN1));
}

TEST(ControllerManagerTest, CreateScsiController)
{
	const int ID = 6;
	auto bus = make_shared<MockBus>();
	ControllerManager controller_manager;

	const auto& controller = controller_manager.CreateScsiController(*bus, ID);
	EXPECT_EQ(ID, controller->GetTargetId());
}

TEST(ControllerManagerTest, AttachToController)
{
	const int ID = 4;
	const int LUN1 = 3;
	const int LUN2 = 0;

	auto bus = make_shared<MockBus>();
	ControllerManager controller_manager;
	DeviceFactory device_factory;

	auto device1 = device_factory.CreateDevice(SCHS, LUN1, "");
	EXPECT_FALSE(controller_manager.AttachToController(*bus, ID, device1)) << "LUN 0 is missing";

	auto device2 = device_factory.CreateDevice(SCLP, LUN2, "");
	EXPECT_TRUE(controller_manager.AttachToController(*bus, ID, device2));
	EXPECT_TRUE(controller_manager.AttachToController(*bus, ID, device1));
	EXPECT_FALSE(controller_manager.AttachToController(*bus, ID, device1));
}

TEST(ControllerManager, ProcessOnController)
{
	ControllerManager controller_manager;

	EXPECT_EQ(AbstractController::piscsi_shutdown_mode::NONE, controller_manager.ProcessOnController(0));
}

TEST(ControllerManager, FlushCaches)
{
	auto bus = make_shared<MockBus>();
	ControllerManager controller_manager;
	auto device1 = make_shared<MockPrimaryDevice>(0);
	auto device2 = make_shared<MockPrimaryDevice>(0);

	EXPECT_TRUE(controller_manager.AttachToController(*bus, 0, device1));
	EXPECT_TRUE(controller_manager.AttachToController(*bus, 4, device2));

	EXPECT_CALL(*device1, FlushCache);
	EXPECT_CALL(*device2, FlushCache);
	controller_manager.FlushCaches();
}
