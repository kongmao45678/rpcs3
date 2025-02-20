#pragma once

#include "hid_pad_handler.h"

#include <unordered_map>

class DualSenseDevice : public HidDevice
{
public:
	enum class DualSenseDataMode
	{
		Simple,
		Enhanced
	};

	bool bt_controller{false};
	u8 bt_sequence{0};
	bool has_calib_data{false};
	std::array<CalibData, CalibIndex::COUNT> calib_data{};
	DualSenseDataMode data_mode{DualSenseDataMode::Simple};
	bool init_lightbar{true};
	bool update_lightbar{true};
	bool update_player_leds{true};

	// Controls for lightbar pulse. This seems somewhat hacky for now, as I haven't found out a nicer way.
	bool lightbar_on{false};
	bool lightbar_on_old{false};
	steady_clock::time_point last_lightbar_time;
};

class dualsense_pad_handler final : public hid_pad_handler<DualSenseDevice>
{
	enum DualSenseKeyCodes
	{
		None = 0,

		Triangle,
		Circle,
		Cross,
		Square,
		Left,
		Right,
		Up,
		Down,
		R1,
		R3,
		L1,
		L3,
		Share,
		Options,
		PSButton,
		Mic,
		TouchPad,

		L2,
		R2,

		LSXNeg,
		LSXPos,
		LSYNeg,
		LSYPos,
		RSXNeg,
		RSXPos,
		RSYNeg,
		RSYPos
	};

public:
	dualsense_pad_handler();
	~dualsense_pad_handler();

	void SetPadData(const std::string& padId, u8 player_id, u32 largeMotor, u32 smallMotor, s32 r, s32 g, s32 b, bool battery_led, u32 battery_led_brightness) override;
	u32 get_battery_level(const std::string& padId) override;
	void init_config(cfg_pad* cfg) override;

private:
	bool get_calibration_data(DualSenseDevice* dualsense_device) const;

	DataStatus get_data(DualSenseDevice* device) override;
	void check_add_device(hid_device* hidDevice, std::string_view path, std::wstring_view wide_serial) override;
	int send_output_report(DualSenseDevice* device) override;

	bool get_is_left_trigger(const std::shared_ptr<PadDevice>& device, u64 keyCode) override;
	bool get_is_right_trigger(const std::shared_ptr<PadDevice>& device, u64 keyCode) override;
	bool get_is_left_stick(const std::shared_ptr<PadDevice>& device, u64 keyCode) override;
	bool get_is_right_stick(const std::shared_ptr<PadDevice>& device, u64 keyCode) override;
	PadHandlerBase::connection update_connection(const std::shared_ptr<PadDevice>& device) override;
	std::unordered_map<u64, u16> get_button_values(const std::shared_ptr<PadDevice>& device) override;
	pad_preview_values get_preview_values(const std::unordered_map<u64, u16>& data) override;
	void get_extended_info(const pad_ensemble& binding) override;
	void apply_pad_data(const pad_ensemble& binding) override;
};
