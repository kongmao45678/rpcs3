// v406 USIO emulator
// Responses may be specific to Taiko no Tatsujin

#include "stdafx.h"
#include "usio.h"
#include "Emu/Cell/lv2/sys_usbd.h"
#include "Input/pad_thread.h"
#include "Emu/System.h"
#include "Emu/IdManager.h"
#include "Emu/system_utils.hpp"

LOG_CHANNEL(usio_log);

struct usio_memory
{
	std::vector<u8> backup_memory;
	std::vector<u8> last_game_status;

	usio_memory(const usio_memory&) = delete;
	usio_memory& operator=(const usio_memory&) = delete;
};

usb_device_usio::usb_device_usio(const std::array<u8, 7>& location)
	: usb_device_emulated(location)
{
	device = UsbDescriptorNode(USB_DESCRIPTOR_DEVICE,
		UsbDeviceDescriptor{
			.bcdUSB             = 0x0110,
			.bDeviceClass       = 0xff,
			.bDeviceSubClass    = 0x00,
			.bDeviceProtocol    = 0xff,
			.bMaxPacketSize0    = 0x8,
			.idVendor           = 0x0b9a,
			.idProduct          = 0x0910,
			.bcdDevice          = 0x0910,
			.iManufacturer      = 0x01,
			.iProduct           = 0x02,
			.iSerialNumber      = 0x00,
			.bNumConfigurations = 0x01});

	auto& config0 = device.add_node(UsbDescriptorNode(USB_DESCRIPTOR_CONFIG,
		UsbDeviceConfiguration{
			.wTotalLength        = 39,
			.bNumInterfaces      = 0x01,
			.bConfigurationValue = 0x01,
			.iConfiguration      = 0x00,
			.bmAttributes        = 0xc0,
			.bMaxPower           = 0x32 // ??? 100ma
		}));

	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_INTERFACE,
		UsbDeviceInterface{
			.bInterfaceNumber   = 0x00,
			.bAlternateSetting  = 0x00,
			.bNumEndpoints      = 0x03,
			.bInterfaceClass    = 0x00,
			.bInterfaceSubClass = 0x00,
			.bInterfaceProtocol = 0x00,
			.iInterface         = 0x00}));

	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_ENDPOINT,
		UsbDeviceEndpoint{
			.bEndpointAddress = 0x01,
			.bmAttributes     = 0x02,
			.wMaxPacketSize   = 0x0040,
			.bInterval        = 0x00}));

	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_ENDPOINT,
		UsbDeviceEndpoint{
			.bEndpointAddress = 0x82,
			.bmAttributes     = 0x02,
			.wMaxPacketSize   = 0x0040,
			.bInterval        = 0x00}));

	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_ENDPOINT,
		UsbDeviceEndpoint{
			.bEndpointAddress = 0x83,
			.bmAttributes     = 0x03,
			.wMaxPacketSize   = 0x0008,
			.bInterval        = 16}));

	g_fxo->get<usio_memory>().backup_memory.resize(0xB8);
	g_fxo->get<usio_memory>().last_game_status.clear();
	g_fxo->get<usio_memory>().last_game_status.resize(0x28);
	load_backup();
}

usb_device_usio::~usb_device_usio()
{
	save_backup();
}

void usb_device_usio::control_transfer(u8 bmRequestType, u8 bRequest, u16 wValue, u16 wIndex, u16 wLength, u32 buf_size, u8* buf, UsbTransfer* transfer)
{
	transfer->fake = true;

	// Control transfers are nearly instant
	switch (bmRequestType)
	{
	default:
		// Follow to default emulated handler
		usb_device_emulated::control_transfer(bmRequestType, bRequest, wValue, wIndex, wLength, buf_size, buf, transfer);
		break;
	}
}

extern bool is_input_allowed();

void usb_device_usio::load_backup()
{
	usio_backup_path = rpcs3::utils::get_hdd1_dir() + "/caches/usiobackup.bin";

	if (!usio_backup_file.open(usio_backup_path, fs::read + fs::write + fs::lock))
	{
		usio_log.trace("Failed to load the USIO Backup file: %s", usio_backup_path);
		return;
	}

	const u64 file_size = g_fxo->get<usio_memory>().backup_memory.size();

	if (usio_backup_file.size() != file_size)
	{
		usio_log.trace("Invalid USIO Backup file detected. Treating it as an empty file: %s", usio_backup_path);
		usio_backup_file.trunc(file_size);
		return;
	}

	usio_backup_file.read(g_fxo->get<usio_memory>().backup_memory.data(), g_fxo->get<usio_memory>().backup_memory.size());
}

void usb_device_usio::save_backup()
{
	if (!usio_backup_file)
	{
		return;
	}

	usio_backup_file.seek(0, fs::seek_set);
	usio_backup_file.write(g_fxo->get<usio_memory>().backup_memory.data(), g_fxo->get<usio_memory>().backup_memory.size());
}

void usb_device_usio::translate_input()
{
	std::lock_guard lock(pad::g_pad_mutex);
	const auto handler = pad::get_current_handler();

	std::vector<u8> input_buf = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	constexpr le_t<u16> c_small_hit = 0x4A0;
	constexpr le_t<u16> c_big_hit = 0xA40;

	auto translate_from_pad = [&](u8 pad_number, u8 player)
	{
		if (!is_input_allowed())
		{
			return;
		}

		const auto& pad = handler->GetPads()[pad_number];
		if (!(pad->m_port_status & CELL_PAD_STATUS_CONNECTED))
		{
			return;
		}

		const std::size_t offset = (player * 8);

		for (const Button& button : pad->m_buttons)
		{
			if (button.m_pressed)
			{
				if (button.m_offset == CELL_PAD_BTN_OFFSET_DIGITAL2)
				{
					switch (button.m_outKeyCode)
					{
					case CELL_PAD_CTRL_SQUARE:
						// Strong hit side left
						std::memcpy(input_buf.data() + 32 + offset, &c_big_hit, sizeof(u16));
						break;
					case CELL_PAD_CTRL_CROSS:
						// Strong hit center right
						std::memcpy(input_buf.data() + 36 + offset, &c_big_hit, sizeof(u16));
						break;
					case CELL_PAD_CTRL_CIRCLE:
						// Strong hit side right
						std::memcpy(input_buf.data() + 38 + offset, &c_big_hit, sizeof(u16));
						break;
					case CELL_PAD_CTRL_TRIANGLE:
						// Strong hit center left
						std::memcpy(input_buf.data() + 34 + offset, &c_big_hit, sizeof(u16));
						break;
					case CELL_PAD_CTRL_L1:
						// Small hit center left
						std::memcpy(input_buf.data() + 34 + offset, &c_small_hit, sizeof(u16));
						break;
					case CELL_PAD_CTRL_R1:
						// Small hit center right
						std::memcpy(input_buf.data() + 36 + offset, &c_small_hit, sizeof(u16));
						break;
					case CELL_PAD_CTRL_L2:
						// Small hit side left
						std::memcpy(input_buf.data() + 32 + offset, &c_small_hit, sizeof(u16));
						break;
					case CELL_PAD_CTRL_R2:
						// Small hit side right
						std::memcpy(input_buf.data() + 38 + offset, &c_small_hit, sizeof(u16));
						break;
					default:
						break;
					}
				}
			}
		}
	};

	translate_from_pad(0, 0);
	translate_from_pad(1, 1);

	q_replies.push(input_buf);
	q_replies.push({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

void usb_device_usio::usio_write(u8 channel, u16 reg, const std::vector<u8>& data)
{
	auto write_memory = [&](std::vector<u8>& memory, bool exact_size = true)
	{
		ensure(data.size() == memory.size() || (data.size() <= memory.size() && !exact_size));
		memcpy(memory.data(), data.data(), data.size());
	};

	const auto get_u16 = [&](std::string_view usio_func) -> u16
	{
		if (data.size() != 2)
		{
			usio_log.fatal("data.size() is %d, expected 2 for get_u16 in %s", data.size(), usio_func);
		}
		return *reinterpret_cast<const le_t<u16>*>(data.data());
	};

	if (channel == 0)
	{
		switch (reg)
		{
		case 0x0002:
		{
			usio_log.notice("SetSystemError: 0x%04X", get_u16("SetSystemError"));
			break;
		}
		case 0x000A:
		{
			u16 command = get_u16("ClearSram");
			ensure(command == 0x6666, "USIO: Unexpected Command instead of ClearSram");
			usio_log.notice("ClearSram");
			break;
		}
		case 0x0028:
		{
			usio_log.notice("SetExpansionMode: 0x%04X", get_u16("SetExpansionMode"));
			break;
		}
		case 0x0048:
		case 0x0058:
		case 0x0068:
		case 0x0078:
		{
			usio_log.notice("SetHopperRequest(Hopper: %d, Request: 0x%04X)", (reg - 0x48) / 0x10, get_u16("SetHopperRequest"));
			break;
		}
		case 0x004A:
		case 0x005A:
		case 0x006A:
		case 0x007A:
		{
			usio_log.notice("SetHopperRequest(Hopper: %d, Limit: 0x%04X)", (reg - 0x4A) / 0x10, get_u16("SetHopperLimit"));
			break;
		}
		default:
		{
			//usio_log.error("Unhandled channel 0 register write: 0x%04X", reg);
			break;
		}
		}
	}
	else if (channel >= 2)
	{
		usio_log.trace("Usio write of sram(chip: %d, addr: 0x%04X)", channel - 2, reg);
		if (channel == 2)
		{
			switch (reg)
			{
			case 0x0000:
			{
				write_memory(g_fxo->get<usio_memory>().backup_memory, false);
				break;
			}
			case 0x0180:
			{
				write_memory(g_fxo->get<usio_memory>().last_game_status);
				break;
			}
			}
		}
	}
	else
	{
		// Channel 1 is the endpoint for firmware update.
		// We are not using any firmware since this is emulation.
		usio_log.warning("Unsupported write operation(channel: 0x%02X, addr: 0x%04X)", channel, reg);
	}
}

void usb_device_usio::usio_read(u8 channel, u16 reg, u16 size)
{
	auto push_zeroes = [&]()
	{
		// Give it 00s
		std::vector<u8> zeroes;
		u16 left = size;
		while (left > 0)
		{
			u16 to_push = std::min(left, static_cast<u16>(64));
			zeroes.resize(to_push);
			q_replies.push(zeroes);
			left -= to_push;
		}
	};

	auto push_vector = [&](std::vector<u8>& memory)
	{
		std::vector<u8> buffer;
		u16 left = size;
		while (left > 0)
		{
			u16 to_push = std::min(left, static_cast<u16>(64));
			buffer.resize(to_push);
			memcpy(buffer.data(), memory.data() + (size - left), buffer.size());
			q_replies.push(buffer);
			left -= to_push;
		}
	};

	if (channel == 0)
	{
		switch (reg)
		{
		case 0x0000:
		{
			// Get Buffer, rarely gives a reply on real HW
			// First U16 seems to be a timestamp of sort
			// Purpose seems related to BananaPass
			q_replies.push({0x7E, 0xE4, 0x00, 0x00, 0x74, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
			break;
		}
		case 0x0080:
		{
			// Purpose unknown
			ensure(size == 0x10);
			q_replies.push({0x02, 0x03, 0x00, 0x00, 0xFF, 0x0F, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x10, 0x00});
			break;
		}
		case 0x1080:
		{
			// Often called, gets input from usio
			translate_input();
			break;
		}
		case 0x1800:
		{
			usio_log.trace("Firmware Query on 0x1800");
			ensure(size == 0x70);
			// Firmware
			// "NBGI.;USIO01;Ver1.00;JPN,Multipurpose with PPG."
			q_replies.push({0x4E, 0x42, 0x47, 0x49, 0x2E, 0x3B, 0x55, 0x53, 0x49, 0x4F, 0x30, 0x31, 0x3B, 0x56, 0x65, 0x72, 0x31, 0x2E, 0x30, 0x30, 0x3B, 0x4A, 0x50, 0x4E, 0x2C, 0x4D, 0x75, 0x6C, 0x74, 0x69, 0x70, 0x75, 0x72, 0x70, 0x6F, 0x73, 0x65, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x50, 0x50, 0x47, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
			q_replies.push({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
			break;
		}
		case 0x1880:
		{
			// Seems to contain a few extra bytes of info in addition to the firmware string
			usio_log.trace("Firmware query on 0x1880");
			ensure(size == 0x70);
			// Firmware
			// "NBGI2;USIO01;Ver1.00;JPN,Multipurpose with PPG."
			q_replies.push({0x4E, 0x42, 0x47, 0x49, 0x32, 0x3B, 0x55, 0x53, 0x49, 0x4F, 0x30, 0x31, 0x3B, 0x56, 0x65, 0x72, 0x31, 0x2E, 0x30, 0x30, 0x3B, 0x4A, 0x50, 0x4E, 0x2C, 0x4D, 0x75, 0x6C, 0x74, 0x69, 0x70, 0x75, 0x72, 0x70, 0x6F, 0x73, 0x65, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x50, 0x50, 0x47, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
			q_replies.push({0x01, 0x00, 0x13, 0x00, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x08, 0xE2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
			break;
		}
		default:
		{
			usio_log.fatal("Unhandled channel 0 register read: 0x%04X", reg);
			break;
		}
		}
	}
	else if (channel >= 2)
	{
		u8 chip = channel - 2;
		usio_log.trace("Usio read of sram(chip: %d, addr: 0x%04X)", chip, reg);
		switch (chip)
		{
		case 0:
		{
			switch (reg)
			{
			case 0x0000:
			{
				ensure(size <= 0xB8);
				push_vector(g_fxo->get<usio_memory>().backup_memory);
				break;
			}
			case 0x0180:
			{
				ensure(size == 0x28);
				push_vector(g_fxo->get<usio_memory>().last_game_status);
				break;
			}
			case 0x0200:
			{
				ensure(size == 0x100);
				// No data returned
				break;
			}
			case 0x1000:
			{
				ensure(size == 0x1000);
				push_zeroes();
				break;
			}
			default:
			{
				usio_log.fatal("Unhandled read of sram(chip: %d, addr: 0x%04X)", channel - 2, reg);
				push_zeroes();
				break;
			}
			}
			break;
		}
		default:
		{
			usio_log.fatal("Unhandled read of sram(chip: %d, addr: 0x%04X)", channel - 2, reg);
			push_zeroes();
			break;
		}
		}
	}
	else
	{
		// Channel 1 is the endpoint for firmware update.
		// We are not using any firmware since this is emulation.
		usio_log.warning("Unsupported read operation(channel: 0x%02X, addr: 0x%04X)", channel, reg);
	}
}

void usb_device_usio::interrupt_transfer(u32 buf_size, u8* buf, u32 endpoint, UsbTransfer* transfer)
{
	constexpr u8 USIO_COMMAND_WRITE = 0x90;
	constexpr u8 USIO_COMMAND_READ  = 0x10;

	static bool expecting_data = false;
	static std::vector<u8> usio_data;
	static u8 usio_channel   = 0;
	static u16 usio_register = 0;
	static u16 usio_length   = 0;

	transfer->fake            = true;
	transfer->expected_result = HC_CC_NOERR;
	// The latency varies per operation but it doesn't seem to matter for this device so let's go fast!
	transfer->expected_time = get_timestamp();

	if (!usio_backup_path.empty() && !usio_backup_file && !usio_backup_file.open(usio_backup_path, fs::create + fs::read + fs::write + fs::lock))
	{
		usio_log.error("Failed to create a new USIO Backup file: %s", usio_backup_path);
	}

	switch (endpoint)
	{
	case 0x01:
	{
		// Write endpoint
		transfer->expected_count = buf_size;

		if (expecting_data)
		{
			usio_data.insert(usio_data.end(), buf, buf + buf_size);
			usio_length -= buf_size;

			if (usio_length == 0)
			{
				expecting_data = false;
				usio_write(usio_channel, usio_register, usio_data);
			}
			return;
		}

		// Commands
		ensure(buf_size == 6, "Expected a command but buf_size != 6");
		usio_channel  = buf[0] & 0xF;
		usio_register = *reinterpret_cast<le_t<u16>*>(&buf[2]);
		usio_length   = *reinterpret_cast<le_t<u16>*>(&buf[4]);

		if ((buf[0] & USIO_COMMAND_WRITE) == USIO_COMMAND_WRITE)
		{
			usio_log.trace("UsioWrite(Channel: 0x%02X, Register: 0x%04X, Length: 0x%04X)", usio_channel, usio_register, usio_length);
			ensure(((~(usio_register >> 8)) & 0xF0) == buf[1]);

			expecting_data = true;
			usio_data.clear();
			return;
		}

		if ((buf[0] & USIO_COMMAND_READ) == USIO_COMMAND_READ)
		{
			usio_log.trace("UsioRead(Channel: 0x%02X, Register: 0x%04X, Length: 0x%04X)", usio_channel, usio_register, usio_length);
			usio_read(usio_channel, usio_register, usio_length);
			return;
		}

		// Init and reset commands
		if ((buf[0] & 0xA0) == 0xA0)
		{
			const std::array<u8, 2> init_command = {0xA0, 0xF0}; // This kind of command starts with 0xA0, 0xF0 commonly. For example, {0xA0, 0xF0, 0x28, 0x00, 0x00, 0x80}
			ensure(memcmp(buf, init_command.data(), 2) == 0);
			return;
		}

		fmt::throw_exception("Received an unexpected command: 0x%02X", buf[0]);
	}
	case 0x82:
	{
		// Read endpoint
		if (!q_replies.empty())
		{
			// Sometimes software will outright ignore what usio sends and read with a buffer of 0
			if (buf_size == 0)
			{
				transfer->expected_count = q_replies.front().size();
				q_replies.pop();
				break;
			}

			// Otherwise we expect the buffer to be appropriately sized
			ensure(buf_size >= q_replies.front().size());
			memcpy(buf, q_replies.front().data(), q_replies.front().size());
			transfer->expected_count = q_replies.front().size();
			q_replies.pop();
		}
		else
		{
			transfer->expected_count = 0;
		}
		break;
	}
	default:
		usio_log.fatal("Unhandled endpoint: 0x%x", endpoint);
		break;
	}
}
