#include "stdafx.h"
#include "pad_thread.h"
#include "product_info.h"
#include "ds3_pad_handler.h"
#include "ds4_pad_handler.h"
#include "dualsense_pad_handler.h"
#ifdef _WIN32
#include "xinput_pad_handler.h"
#include "mm_joystick_handler.h"
#elif HAVE_LIBEVDEV
#include "evdev_joystick_handler.h"
#endif
#include "keyboard_pad_handler.h"
#include "Emu/Io/Null/NullPadHandler.h"
#include "Emu/Io/PadHandler.h"
#include "Emu/Io/pad_config.h"
#include "Emu/System.h"
#include "Emu/system_config.h"
#include "Utilities/Thread.h"
#include "util/atomic.hpp"

LOG_CHANNEL(input_log, "Input");

extern bool is_input_allowed();

namespace pad
{
	atomic_t<pad_thread*> g_current = nullptr;
	shared_mutex g_pad_mutex;
	std::string g_title_id;
	atomic_t<bool> g_started{false};
	atomic_t<bool> g_reset{false};
	atomic_t<bool> g_enabled{true};
}

struct pad_setting
{
	u32 port_status = 0;
	u32 device_capability = 0;
	u32 device_type = 0;
	bool is_ldd_pad = false;
};

pad_thread::pad_thread(void* curthread, void* curwindow, std::string_view title_id) : m_curthread(curthread), m_curwindow(curwindow)
{
	pad::g_title_id = title_id;
	pad::g_current = this;
	pad::g_started = false;
}

pad_thread::~pad_thread()
{
	pad::g_current = nullptr;
}

void pad_thread::Init()
{
	std::lock_guard lock(pad::g_pad_mutex);

	// Cache old settings if possible
	std::array<pad_setting, CELL_PAD_MAX_PORT_NUM> pad_settings;
	for (u32 i = 0; i < CELL_PAD_MAX_PORT_NUM; i++) // max 7 pads
	{
		if (m_pads[i])
		{
			pad_settings[i] =
			{
				m_pads[i]->m_port_status,
				m_pads[i]->m_device_capability,
				m_pads[i]->m_device_type,
				m_pads[i]->ldd
			};
		}
		else
		{
			pad_settings[i] =
			{
				CELL_PAD_STATUS_DISCONNECTED,
				CELL_PAD_CAPABILITY_PS3_CONFORMITY | CELL_PAD_CAPABILITY_PRESS_MODE | CELL_PAD_CAPABILITY_ACTUATOR,
				CELL_PAD_DEV_TYPE_STANDARD,
				false
			};
		}
	}

	num_ldd_pad = 0;

	m_info.now_connect = 0;

	handlers.clear();

	g_cfg_profile.load();

	std::string active_profile = g_cfg_profile.active_profiles.get_value(pad::g_title_id);
	if (active_profile.empty())
	{
		active_profile = g_cfg_profile.active_profiles.get_value(g_cfg_profile.global_key);
	}

	input_log.notice("Using pad profile: '%s'", active_profile);

	// Load in order to get the pad handlers
	if (!g_cfg_input.load(pad::g_title_id, active_profile))
	{
		input_log.notice("Loaded empty pad config");
	}

	// Adjust to the different pad handlers
	for (usz i = 0; i < g_cfg_input.player.size(); i++)
	{
		std::shared_ptr<PadHandlerBase> handler;
		pad_thread::InitPadConfig(g_cfg_input.player[i]->config, g_cfg_input.player[i]->handler, handler);
	}

	// Reload with proper defaults
	if (!g_cfg_input.load(pad::g_title_id, active_profile))
	{
		input_log.notice("Reloaded empty pad config");
	}

	input_log.trace("Using pad config:\n%s", g_cfg_input.to_string());

	std::shared_ptr<keyboard_pad_handler> keyptr;

	// Always have a Null Pad Handler
	std::shared_ptr<NullPadHandler> nullpad = std::make_shared<NullPadHandler>();
	handlers.emplace(pad_handler::null, nullpad);

	for (u32 i = 0; i < CELL_PAD_MAX_PORT_NUM; i++) // max 7 pads
	{
		cfg_player* cfg = g_cfg_input.player[i];
		std::shared_ptr<PadHandlerBase> cur_pad_handler;

		const pad_handler handler_type = pad_settings[i].is_ldd_pad ? pad_handler::null : cfg->handler.get();

		if (handlers.contains(handler_type))
		{
			cur_pad_handler = handlers[handler_type];
		}
		else
		{
			switch (handler_type)
			{
			case pad_handler::keyboard:
				keyptr = std::make_shared<keyboard_pad_handler>();
				keyptr->moveToThread(static_cast<QThread*>(m_curthread));
				keyptr->SetTargetWindow(static_cast<QWindow*>(m_curwindow));
				cur_pad_handler = keyptr;
				break;
			case pad_handler::ds3:
				cur_pad_handler = std::make_shared<ds3_pad_handler>();
				break;
			case pad_handler::ds4:
				cur_pad_handler = std::make_shared<ds4_pad_handler>();
				break;
			case pad_handler::dualsense:
				cur_pad_handler = std::make_shared<dualsense_pad_handler>();
				break;
#ifdef _WIN32
			case pad_handler::xinput:
				cur_pad_handler = std::make_shared<xinput_pad_handler>();
				break;
			case pad_handler::mm:
				cur_pad_handler = std::make_shared<mm_joystick_handler>();
				break;
#endif
#ifdef HAVE_LIBEVDEV
			case pad_handler::evdev:
				cur_pad_handler = std::make_shared<evdev_joystick_handler>();
				break;
#endif
			case pad_handler::null:
				break;
			}
			handlers.emplace(handler_type, cur_pad_handler);
		}
		cur_pad_handler->Init();

		m_pads[i] = std::make_shared<Pad>(handler_type, CELL_PAD_STATUS_DISCONNECTED, pad_settings[i].device_capability, pad_settings[i].device_type);

		if (pad_settings[i].is_ldd_pad)
		{
			InitLddPad(i, &pad_settings[i].port_status);
		}
		else
		{
			if (!cur_pad_handler->bindPadToDevice(m_pads[i], i))
			{
				// Failed to bind the device to cur_pad_handler so binds to NullPadHandler
				input_log.error("Failed to bind device '%s' to handler %s. Falling back to NullPadHandler.", cfg->device.to_string(), handler_type);
				nullpad->bindPadToDevice(m_pads[i], i);
			}

			input_log.notice("Pad %d: device='%s', handler=%s, VID=0x%x, PID=0x%x, class_type=0x%x, class_profile=0x%x",
				i, cfg->device.to_string(), m_pads[i]->m_pad_handler, m_pads[i]->m_vendor_id, m_pads[i]->m_product_id, m_pads[i]->m_class_type, m_pads[i]->m_class_profile);
		}
	}
}

void pad_thread::SetRumble(const u32 pad, u8 largeMotor, bool smallMotor)
{
	if (pad >= m_pads.size())
		return;

	if (m_pads[pad]->m_vibrateMotors.size() >= 2)
	{
		m_pads[pad]->m_vibrateMotors[0].m_value = largeMotor;
		m_pads[pad]->m_vibrateMotors[1].m_value = smallMotor ? 255 : 0;
	}
}

void pad_thread::SetIntercepted(bool intercepted)
{
	if (intercepted)
	{
		m_info.system_info |= CELL_PAD_INFO_INTERCEPTED;
		m_info.ignore_input = true;
	}
	else
	{
		m_info.system_info &= ~CELL_PAD_INFO_INTERCEPTED;
	}
}

void pad_thread::operator()()
{
	Init();

	pad::g_reset = false;
	pad::g_started = true;

	bool mode_changed = true;

	atomic_t<pad_handler_mode> pad_mode{g_cfg.io.pad_mode.get()};
	std::vector<std::unique_ptr<named_thread<std::function<void()>>>> threads;

	const auto stop_threads = [&threads]()
	{
		input_log.notice("Stopping pad threads...");

		for (auto& thread : threads)
		{
			if (thread)
			{
				auto& enumeration_thread = *thread;
				enumeration_thread = thread_state::aborting;
				enumeration_thread();
			}
		}
		threads.clear();

		input_log.notice("Pad threads stopped");
	};

	const auto start_threads = [this, &threads, &pad_mode]()
	{
		if (pad_mode == pad_handler_mode::single_threaded)
		{
			return;
		}

		input_log.notice("Starting pad threads...");

		for (const auto& handler : handlers)
		{
			if (handler.first == pad_handler::null)
			{
				continue;
			}

			threads.push_back(std::make_unique<named_thread<std::function<void()>>>(fmt::format("%s Thread", handler.second->m_type), [&handler = handler.second, &pad_mode]()
			{
				while (thread_ctrl::state() != thread_state::aborting)
				{
					if (!pad::g_enabled || Emu.IsPaused() || !is_input_allowed())
					{
						thread_ctrl::wait_for(10'000);
						continue;
					}

					handler->ThreadProc();

					thread_ctrl::wait_for(g_cfg.io.pad_sleep);
				}
			}));
		}

		input_log.notice("Pad threads started");
	};

	while (thread_ctrl::state() != thread_state::aborting)
	{
		if (!pad::g_enabled || Emu.IsPaused() || !is_input_allowed())
		{
			thread_ctrl::wait_for(10000);
			continue;
		}

		// Update variables
		const bool needs_reset = pad::g_reset && pad::g_reset.exchange(false);
		mode_changed |= pad_mode != pad_mode.exchange(g_cfg.io.pad_mode.get());

		// Reset pad handlers if necessary
		if (needs_reset || mode_changed)
		{
			mode_changed = false;

			stop_threads();

			if (needs_reset)
			{
				Init();
			}
			else
			{
				input_log.success("The pad mode was changed to %s", pad_mode.load());
			}

			start_threads();
		}

		u32 connected_devices = 0;

		if (pad_mode == pad_handler_mode::single_threaded)
		{
			for (auto& handler : handlers)
			{
				handler.second->ThreadProc();
				connected_devices += handler.second->connected_devices;
			}
		}
		else
		{
			for (auto& handler : handlers)
			{
				connected_devices += handler.second->connected_devices;
			}
		}

		m_info.now_connect = connected_devices + num_ldd_pad;

		// The ignore_input section is only reached when a dialog was closed and the pads are still intercepted.
		// As long as any of the listed buttons is pressed, cellPadGetData will ignore all input (needed for Hotline Miami).
		// ignore_input was added because if we keep the pads intercepted, then some games will enter the menu due to unexpected system interception (tested with Ninja Gaiden Sigma).
		if (m_info.ignore_input && !(m_info.system_info & CELL_PAD_INFO_INTERCEPTED))
		{
			bool any_button_pressed = false;

			for (usz i = 0; i < m_pads.size() && !any_button_pressed; i++)
			{
				const auto& pad = m_pads[i];

				if (!(pad->m_port_status & CELL_PAD_STATUS_CONNECTED))
					continue;

				for (auto& button : pad->m_buttons)
				{
					if (button.m_pressed && (
						button.m_outKeyCode == CELL_PAD_CTRL_CROSS ||
						button.m_outKeyCode == CELL_PAD_CTRL_CIRCLE ||
						button.m_outKeyCode == CELL_PAD_CTRL_TRIANGLE ||
						button.m_outKeyCode == CELL_PAD_CTRL_SQUARE ||
						button.m_outKeyCode == CELL_PAD_CTRL_START ||
						button.m_outKeyCode == CELL_PAD_CTRL_SELECT))
					{
						any_button_pressed = true;
						break;
					}
				}
			}

			if (!any_button_pressed)
			{
				m_info.ignore_input = false;
			}
		}

		thread_ctrl::wait_for(g_cfg.io.pad_sleep);
	}

	stop_threads();
}

void pad_thread::InitLddPad(u32 handle, const u32* port_status)
{
	if (handle >= m_pads.size())
	{
		return;
	}

	static const input::product_info product = input::get_product_info(input::product_type::playstation_3_controller);

	m_pads[handle]->ldd = true;
	m_pads[handle]->Init
	(
		port_status ? *port_status : CELL_PAD_STATUS_CONNECTED | CELL_PAD_STATUS_ASSIGN_CHANGES | CELL_PAD_STATUS_CUSTOM_CONTROLLER,
		CELL_PAD_CAPABILITY_PS3_CONFORMITY,
		CELL_PAD_DEV_TYPE_LDD,
		0, // CELL_PAD_PCLASS_TYPE_STANDARD
		product.pclass_profile,
		product.vendor_id,
		product.product_id,
		50
	);

	input_log.notice("Pad %d: LDD, VID=0x%x, PID=0x%x, class_type=0x%x, class_profile=0x%x",
		handle, m_pads[handle]->m_vendor_id, m_pads[handle]->m_product_id, m_pads[handle]->m_class_type, m_pads[handle]->m_class_profile);

	num_ldd_pad++;
}

s32 pad_thread::AddLddPad()
{
	// Look for first null pad
	for (u32 i = 0; i < CELL_PAD_MAX_PORT_NUM; i++)
	{
		if (g_cfg_input.player[i]->handler == pad_handler::null && !m_pads[i]->ldd)
		{
			InitLddPad(i, nullptr);
			return i;
		}
	}

	return -1;
}

void pad_thread::UnregisterLddPad(u32 handle)
{
	ensure(handle < m_pads.size());

	m_pads[handle]->ldd = false;

	num_ldd_pad--;
}

std::shared_ptr<PadHandlerBase> pad_thread::GetHandler(pad_handler type)
{
	switch (type)
	{
	case pad_handler::null:
		return std::make_unique<NullPadHandler>();
	case pad_handler::keyboard:
		return std::make_unique<keyboard_pad_handler>();
	case pad_handler::ds3:
		return std::make_unique<ds3_pad_handler>();
	case pad_handler::ds4:
		return std::make_unique<ds4_pad_handler>();
	case pad_handler::dualsense:
		return std::make_unique<dualsense_pad_handler>();
#ifdef _WIN32
	case pad_handler::xinput:
		return std::make_unique<xinput_pad_handler>();
	case pad_handler::mm:
		return std::make_unique<mm_joystick_handler>();
#endif
#ifdef HAVE_LIBEVDEV
	case pad_handler::evdev:
		return std::make_unique<evdev_joystick_handler>();
#endif
	}

	return nullptr;
}

void pad_thread::InitPadConfig(cfg_pad& cfg, pad_handler type, std::shared_ptr<PadHandlerBase>& handler)
{
	if (!handler)
	{
		handler = GetHandler(type);
	}

	ensure(!!handler);
	handler->init_config(&cfg);
}
