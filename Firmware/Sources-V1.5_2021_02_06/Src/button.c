/*
 * button.c
 *
 *  Created on: 28.03.2017.
 *      Author: milan
 */
// ----------------------------------------------------------------------------
// Include section - add all #includes here:

#include "main.h"

#include "system_conf.h"
#include "iodrv.h"
#include "time_count.h"
#include "nv.h"
#include "util.h"

#include "button.h"


// ----------------------------------------------------------------------------
// Defines section - add all #defines here:


// ----------------------------------------------------------------------------
// Function prototypes for functions that only have scope in this module:

static void BUTTON_SetConfigData(Button_T * const p_button, const uint8_t pinref);
static void BUTTON_UpdateConfigData(Button_T * const p_button, const Button_T * const configData);
static ButtonFunction_T BUTTON_GetEventFunc(Button_T * const p_button);
static void BUTTON_ProcessButton(Button_T * const p_button, const uint32_t sysTick);
static void BUTTON_SetConfigData(Button_T * const p_button, const uint8_t pinref);
static bool BUTTON_ReadConfigFromNv(const uint8_t buttonIndex);
static void BUTTON_WriteConfigToNv(const Button_T * const p_configData, const uint8_t buttonIdx);


// ----------------------------------------------------------------------------
// Variables that only have scope in this module:

static Button_T m_buttons[3u] =
{
	{ // sw1
		.pressFunc = BUTTON_EVENT_NO_FUNC,
		.pressConfig = 0u,
		.releaseFunc = BUTTON_EVENT_NO_FUNC,
		.releaseConfig = 0u,
		.singlePressFunc = BUTTON_EVENT_FUNC_POWER_ON,
		.singlePressTime = 800u,
		.doublePressFunc = BUTTON_EVENT_NO_FUNC,
		.doublePressTime = 0u,
		.longPressFunc1 = (BUTTON_EVENT_FUNC_SYS_EVENT| 1u),
		.longPressTime1 = 10000u,
		.longPressFunc2 = BUTTON_EVENT_FUNC_POWER_OFF,
		.longPressTime2 = 20000u,
		.index = 0u
	},
	{ // sw2
		.pressFunc = BUTTON_EVENT_NO_FUNC,
		.pressConfig = 0u,
		.releaseFunc = BUTTON_EVENT_NO_FUNC,
		.releaseConfig = 0u,
		.singlePressFunc = (BUTTON_EVENT_FUNC_USER_EVENT | 1u),
		.singlePressTime = 400u,
		.doublePressFunc = (BUTTON_EVENT_FUNC_USER_EVENT | 2u),
		.doublePressTime = 600u,
		.longPressFunc1 = BUTTON_EVENT_NO_FUNC,
		.longPressTime1 = 0u,
		.longPressFunc2 = BUTTON_EVENT_NO_FUNC,
		.longPressTime2 = 0u,
		.index = 1u
	},
	{ // sw3
		.pressFunc = (BUTTON_EVENT_FUNC_USER_EVENT | 3u),
		.pressConfig = 0u,
		.releaseFunc = (BUTTON_EVENT_FUNC_USER_EVENT | 4u),
		.releaseConfig = 0u,
		.singlePressFunc = (BUTTON_EVENT_FUNC_USER_EVENT | 5u),
		.singlePressTime = 1000u,
		.doublePressFunc = (BUTTON_EVENT_FUNC_USER_EVENT | 6u),
		.doublePressTime = 1500u,
		.longPressFunc1 = (BUTTON_EVENT_FUNC_USER_EVENT | 7u),
		.longPressTime1 = 5000u,
		.longPressFunc2 = (BUTTON_EVENT_FUNC_USER_EVENT | 8u),
		.longPressTime2 = 10000u,
		.index = 2u
	}
};

static ButtonEventCb_T m_buttonEventCallbacks[BUTTON_EVENT_FUNC_COUNT] =
{
	NULL, // BUTTON_EVENT_NO_FUNC
	BUTTON_PowerOnEventCb, // BUTTON_EVENT_FUNC_POWER_ON
	BUTTON_PowerOffEventCb, // BUTTON_EVENT_FUNC_POWER_OFF
	BUTTON_PowerResetEventCb, // BUTTON_EVENT_FUNC_POWER_RESET
};


static int8_t m_writebuttonConfigData = -1;
static Button_T m_buttonConfigData;


// ----------------------------------------------------------------------------
// Variables that have scope from outside this module:


// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// FUNCTIONS WITH GLOBAL SCOPE
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ****************************************************************************
/*!
 * BUTTON_Init configures the buttons with their respective IO pin
 *
 * @param	none
 * @retval	none
 */
// ****************************************************************************
void BUTTON_Init(void)
{
	if (true == BUTTON_ReadConfigFromNv(0u))
	{
		BUTTON_SetConfigData(&m_buttons[0u], IODRV_PIN_SW1);
	}

	if (true == BUTTON_ReadConfigFromNv(1u))
	{
		BUTTON_SetConfigData(&m_buttons[1u], IODRV_PIN_SW2);
	}

	if (true == BUTTON_ReadConfigFromNv(2u))
	{
		BUTTON_SetConfigData(&m_buttons[2u], IODRV_PIN_SW3);
	}
}


// ****************************************************************************
/*!
 * BUTTON_Task performs the periodic updates, processing the button state and writing
 * configuration data if a change to the data has occurred.
 *
 * @param	none
 * @retval	none
 */
// ****************************************************************************
void BUTTON_Task(void)
{
	const uint32_t sysTime = HAL_GetTick();
	const uint8_t oldDualLongPressStatus = m_buttons[0u].staticLongPressEvent && m_buttons[1u].staticLongPressEvent;

	BUTTON_ProcessButton(&m_buttons[0u], sysTime); // sw1
	BUTTON_ProcessButton(&m_buttons[1u], sysTime); // sw2
	BUTTON_ProcessButton(&m_buttons[2u], sysTime); // sw3

	// Check to see if both buttons SW1 and SW2 have been held down at the same time
	if ((m_buttons[0u].staticLongPressEvent && m_buttons[1u].staticLongPressEvent) > oldDualLongPressStatus)
	{
		BUTTON_DualLongPressEventCb();
	}

	if (m_writebuttonConfigData >= 0)
	{
		if (m_writebuttonConfigData < BUTTON_MAX_BUTTONS)
		{
			BUTTON_WriteConfigToNv(&m_buttonConfigData, (uint8_t)m_writebuttonConfigData);

			if (true == BUTTON_ReadConfigFromNv((uint8_t)m_writebuttonConfigData))
			{
				BUTTON_UpdateConfigData(&m_buttons[m_writebuttonConfigData], &m_buttonConfigData);
			}
		}

		m_writebuttonConfigData = -1;
	}
}


// ****************************************************************************
/*!
 * BUTTON_ProcessButton checks to see if an event is required on a button. Following
 * the previous routine it is implemented as such:
 *
 * BUTTON_EVENT_PRESS is raised on first rising edge after 30 seconds of no activity.
 * BUTTON_EVENT_RELEASE is raised on first falling edge if time exceeds single press or no action is registered for single press.
 * BUTTON_EVENT_SINGLE_PRESS is raised after button is released and does not exceed button.singlePress time
 * BUTTON_EVENT_DOUBLE_PRESS is raised on second rising edge if the second edge occurs before button.doublePressTime
 * BUTTON_EVENT_LONG_PRESS1 is raised if the button is held and the button.longPressTime1 is exceeded.
 * BUTTON_EVENT_LONG_PRESS2 is raised if the button is held and the button.longPressTime2 is exceeded.
 *
 * In all cases: Events are not registered if actions are not allocated to the function.
 * In all cases: Events are ALWAYS raised if the previous priority is exceeded with exception of BUTTON_EVENT_SINGLE_PRESS.
 *
 * Things get a bit sketchy if the event is cleared and the button is held.
 *
 * Activity is cleared after 30 seconds.
 *
 * Double press is not registered on first rising edge because the timer has not been initialised and it is expected that the
 * timer is greater than double press time.
 *
 * staticButtonLongPress is evaluated to true when the button is held and time exceeds BUTTON_STATIC_LONG_PRESS_TIME
 * staticButtonLongPress is reset on button release
 *
 *
 * @param	p_button		pointer to button to process
 * @param	sysTick			current value of the system tick timer
 * @retval	none
 */
// ****************************************************************************
void BUTTON_ProcessButton(Button_T * const p_button, const uint32_t sysTick)
{
	if ( (NULL == p_button) || (NULL == p_button->p_pinInfo) )
	{
		return;
	}

	const uint32_t previousButtonCycleTimeMs = p_button->p_pinInfo->lastPosPulseWidthTimeMs
										+ p_button->p_pinInfo->lastNegPulseWidthTimeMs;

	const uint32_t lastEdgeMs = MS_TIMEREF_DIFF(p_button->p_pinInfo->lastDigitalChangeTime, sysTick);

	ButtonEvent_T oldEv = p_button->event;
	ButtonFunction_T func;

	// Copy value to ensure compatibility
	p_button->state = p_button->p_pinInfo->value;

	if ( (GPIO_PIN_RESET == p_button->p_pinInfo->value) )
	{
		if (lastEdgeMs > BUTTON_EVENT_EXPIRE_PERIOD_MS)
		{
			// Event timeout, remove it
			p_button->event = BUTTON_EVENT_NONE;
			oldEv = BUTTON_EVENT_NONE;
		}

		// The pulse is cleared after one day in the IODRV module
		// This check ensures that the 47 day roll over does not cause a phantom
		// button press as the button will have appeared to have been pressed
		// recently when it hasn't!
		else if (p_button->p_pinInfo->lastPosPulseWidthTimeMs > 0u)
		{

			// Check 	- single press time has not been exceeded for last press cycle
			//			- double press time has been exceeded
			//			- single press function has been allocated
			//			- single press function has not already been executed
			if ( (p_button->p_pinInfo->lastPosPulseWidthTimeMs < p_button->singlePressTime)
					&& ((lastEdgeMs + p_button->p_pinInfo->lastPosPulseWidthTimeMs) > p_button->doublePressTime)
					&& (BUTTON_EVENT_NO_FUNC != p_button->singlePressFunc)
					&& (p_button->event < BUTTON_EVENT_SINGLE_PRESS))
			{
				// Raise single press event
				p_button->event = BUTTON_EVENT_SINGLE_PRESS;
			}


			if ( (p_button->event < BUTTON_EVENT_RELEASE) && (BUTTON_EVENT_NO_FUNC != p_button->releaseFunc) )
			{
				// Raise release event
				p_button->event = BUTTON_EVENT_RELEASE;
			}
		}

		p_button->staticLongPressEvent = 0u;
	}
	else
	{
		/* Button is pressed and held */

		if ( (lastEdgeMs > p_button->longPressTime2) && (BUTTON_EVENT_NO_FUNC != p_button->longPressFunc2) )
		{
			// Raise long press 2
			p_button->event = BUTTON_EVENT_LONG_PRESS2;
		}
		else if ( (lastEdgeMs > p_button->longPressTime1) && (BUTTON_EVENT_NO_FUNC != p_button->longPressFunc1) )
		{
			// Raise long press 1
			p_button->event = BUTTON_EVENT_LONG_PRESS1;
		}
		else if ( ((lastEdgeMs + previousButtonCycleTimeMs) < p_button->doublePressTime)
				&& (previousButtonCycleTimeMs > 0u)
				&& (BUTTON_EVENT_NO_FUNC != p_button->doublePressFunc))
		{
			// Raise double press event
			p_button->event = BUTTON_EVENT_DOUBLE_PRESS;
		}

		if ( lastEdgeMs > BUTTON_STATIC_LONG_PRESS_TIME)
		{
			p_button->staticLongPressEvent = 1u;
		}

		if ( (p_button->event < BUTTON_EVENT_PRESS) && (BUTTON_EVENT_NO_FUNC != p_button->pressFunc) )
		{
			// Raise button press event
			p_button->event = BUTTON_EVENT_PRESS;
		}
	}

	// Check if event has already been processed
	if ( p_button->event > oldEv )
	{
	    func = BUTTON_GetEventFunc(p_button);

		if ( (func < BUTTON_EVENT_FUNC_COUNT) && (m_buttonEventCallbacks[func] != NULL) )
		{
			m_buttonEventCallbacks[func](p_button);
		}
	}
}


// ****************************************************************************
/*!
 * BUTTON_GetButtonEvent returns the event id of an indexed button. The index must
 * be valid or BUTTON_EVENT_NONE will be returned.
 *
 * @param	buttonIndex			index of addressed button
 * @retval	ButtonEvent_T		event id assigned to the current event.
 */
// ****************************************************************************
ButtonEvent_T BUTTON_GetButtonEvent(const uint8_t buttonIndex)
{
	return (buttonIndex < BUTTON_MAX_BUTTONS) ? m_buttons[buttonIndex].event : BUTTON_EVENT_NONE;
}


// ****************************************************************************
/*!
 * BUTTON_ClearEvent clears the event for an indexed button, the button index is
 * checked for validity and the IO pin edges are cleared to ensure a further false
 * event trigger is not processed.
 *
 * @param	buttonIndex			index of addressed button
 * @retval	none
 */
// ****************************************************************************
void BUTTON_ClearEvent(const uint8_t buttonIndex)
{
	if (buttonIndex < BUTTON_MAX_BUTTONS)
	{
		m_buttons[buttonIndex].event = BUTTON_EVENT_NONE;

		// Ensure the event doesn't get re-triggered
		IORDV_ClearPinEdges(m_buttons[buttonIndex].p_pinInfo->index);
	}
}


// ****************************************************************************
/*!
 * BUTTON_IsEventActive tells the caller if a button has an event currently active
 * or if a button is being monitored. As the event is only processed fully once the
 * button is released or a valid function assigned to that event so the event could
 * escalate further before being truly active.
 *
 * @param	none
 * @retval	bool
 */
// ****************************************************************************
bool BUTTON_IsEventActive(void)
{
	return (BUTTON_EVENT_NONE != m_buttons[0u].event) ||
			(BUTTON_EVENT_NONE != m_buttons[1u].event) ||
			(BUTTON_EVENT_NONE != m_buttons[2u].event);
}


// ****************************************************************************
/*!
 * BUTTON_IsButtonActive tells the caller if any button is being held by returning
 * true or false. It does not give any information about which button is being held
 * or if a button is being monitored for double press.
 *
 * @param	none
 * @retval	bool		false = button not held
 * 						true = button is being held
 */
// ****************************************************************************
bool BUTTON_IsButtonActive(void)
{
	return (GPIO_PIN_SET == m_buttons[0u].state) || (GPIO_PIN_SET ==  m_buttons[1].state) || (GPIO_PIN_SET == m_buttons[2].state);
}


// ****************************************************************************
/*!
 * BUTTON_SetConfigurationData sets the configuration data for an indexed button,
 * the assingment itself is handled by the task routine of this module. The data
 * is placed in an intermediate configuration container and then m_writebuttonConfigData
 * is set to the button the task will configure. No checks are made to ensure a
 * button is already expected to be configured so if called too quickly the chances
 * are a button may not be configured when expected.
 *
 * @param	buttonIndex			index of addressed button
 * @param	p_data				pointer to configuration data
 * @param	len					length of configuration data
 * @retval	none
 */
// ****************************************************************************
void BUTTON_SetConfigurationData(const uint8_t buttonIndex, const uint8_t * const p_data,
									const uint8_t len)
{
	if (buttonIndex > BUTTON_LAST_BUTTON_IDX)
	{
		return;
	}

	m_buttonConfigData.pressFunc = (ButtonFunction_T)p_data[0u];
	m_buttonConfigData.pressConfig = p_data[1u];
	m_buttonConfigData.releaseFunc = (ButtonFunction_T)p_data[2u];
	m_buttonConfigData.releaseConfig = p_data[3u];
	m_buttonConfigData.singlePressFunc = (ButtonFunction_T)p_data[4u];
	m_buttonConfigData.singlePressTime = p_data[5u];
	m_buttonConfigData.doublePressFunc = (ButtonFunction_T)p_data[6u];
	m_buttonConfigData.doublePressTime = p_data[7u];
	m_buttonConfigData.longPressFunc1 = (ButtonFunction_T)p_data[8u];
	m_buttonConfigData.longPressTime1 = p_data[9u];
	m_buttonConfigData.longPressFunc2 = (ButtonFunction_T)p_data[10u];
	m_buttonConfigData.longPressTime2 = p_data[11u];

	m_writebuttonConfigData = buttonIndex;
}


// ****************************************************************************
/*!
 * BUTTON_GetConfigurationData gets the configuration data from an addressed
 * button, the time values are converted to 0.1S resolution. The button index is
 * checked to ensure a button exists at that index.
 *
 * @param	buttonIndex			index of addressed button
 * @param	p_data				pointer to destination for configuration data
 * @param	p_len				pointer to destination for data length
 * @retval	bool				false = configuration data invalid
 * 								true = configuration data valid
 */
// ****************************************************************************
bool BUTTON_GetConfigurationData(const uint8_t buttonIndex, uint8_t * const p_data,
									uint16_t * const p_len)
{
	if (buttonIndex > BUTTON_LAST_BUTTON_IDX)
	{
		*p_len = 0u;
		return false;
	}

	const Button_T* p_button = &m_buttons[buttonIndex];

	p_data[0u] = (uint8_t)p_button->pressFunc;
	p_data[1u] = (uint8_t)p_button->pressConfig;
	p_data[2u] = (uint8_t)p_button->releaseFunc;
	p_data[3u] = (uint8_t)p_button->releaseConfig;
	p_data[4u] = (uint8_t)p_button->singlePressFunc;
	p_data[5u] = (uint8_t)(p_button->singlePressTime / 100u);
	p_data[6u] = (uint8_t)p_button->doublePressFunc;
	p_data[7u] = (uint8_t)(p_button->doublePressTime / 100u);
	p_data[8u] = (uint8_t)p_button->longPressFunc1;
	p_data[9u] = (uint8_t)(p_button->longPressTime1 / 100u);
	p_data[10u] = (uint8_t)p_button->longPressFunc2;
	p_data[11u] = (uint8_t)(p_button->longPressTime2 / 100u);

	*p_len = 12u;

	return true;
}


// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// FUNCTIONS WITH LOCAL SCOPE
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ****************************************************************************
/*!
 * BUTTON_ReadConfigFromNv gets the stored configuration from non volatile memory
 * and populates the configuration intermediate storage. The time values are in
 * 0.1S resolution and will need to be converted to mS in its final destination.
 *
 * @param	buttonIndex			index of addressed button
 * @retval	bool				false = configuration data invalid
 * 								true = configuration data appears valid
 */
// ****************************************************************************
static bool BUTTON_ReadConfigFromNv(const uint8_t buttonIndex)
{
	const uint8_t nvOffset = BUTTON_PRESS_FUNC_SW1 +
								(buttonIndex * (BUTTON_PRESS_FUNC_SW2 - BUTTON_PRESS_FUNC_SW1));
	bool dataValid = true;

	dataValid &= NV_ReadVariable_U8(nvOffset, (uint8_t*)&m_buttonConfigData.pressFunc);
	dataValid &= NV_ReadVariable_U8(nvOffset + 2u, (uint8_t*)&m_buttonConfigData.releaseFunc);
	dataValid &= NV_ReadVariable_U8(nvOffset + 4u, (uint8_t*)&m_buttonConfigData.singlePressFunc);
	dataValid &= NV_ReadVariable_U8(nvOffset + 5u, (uint8_t*)&m_buttonConfigData.singlePressTime);
	dataValid &= NV_ReadVariable_U8(nvOffset + 6u, (uint8_t*)&m_buttonConfigData.doublePressFunc);
	dataValid &= NV_ReadVariable_U8(nvOffset + 7u, (uint8_t*)&m_buttonConfigData.doublePressTime);
	dataValid &= NV_ReadVariable_U8(nvOffset + 8u, (uint8_t*)&m_buttonConfigData.longPressFunc1);
	dataValid &= NV_ReadVariable_U8(nvOffset + 9u, (uint8_t*)&m_buttonConfigData.longPressTime1);
	dataValid &= NV_ReadVariable_U8(nvOffset + 10u, (uint8_t*)&m_buttonConfigData.longPressFunc2);
	dataValid &= NV_ReadVariable_U8(nvOffset + 11u, (uint8_t*)&m_buttonConfigData.longPressTime2);

	return dataValid;
}


// ****************************************************************************
/*!
 * BUTTON_SetConfigData sets up the button with its configuration data that has
 * been collected from NV memory or sent by the host in a new configuration. The
 * IO pin is assigned by linking the iodrv pin handle to the button struct. No
 * checks are performed for data validity or null pointers.
 *
 * @param	p_button		pointer to destination button
 * @param	pinref			pointer to the iodrv pin handle
 * @retval	none
 */
// ****************************************************************************
static void BUTTON_SetConfigData(Button_T * const p_button, const uint8_t pinref)
{
	p_button->p_pinInfo = IODRV_GetPinInfo(pinref);

	BUTTON_UpdateConfigData(p_button, &m_buttonConfigData);
}


// ****************************************************************************
/*!
 * BUTTON_UpdateConfigData moves the configuration data into the button. No checks
 * are made for data validity or null pointers. The times that are sent over are
 * multiplied by 100 to give a 0.1S resolution in the configuration data.
 *
 * @param	p_button			pointer to destination button
 * @param	p_configData		pointer to button configuration
 * @retval	none
 */
// ****************************************************************************
static void BUTTON_UpdateConfigData(Button_T * const p_button,
										const Button_T * const p_configData)
{
	p_button->pressFunc = p_configData->pressFunc;
	p_button->releaseFunc = p_configData->releaseFunc;
	p_button->singlePressFunc = p_configData->singlePressFunc;
	p_button->singlePressTime = p_configData->singlePressTime * 100u;
	p_button->doublePressFunc = p_configData->doublePressFunc;
	p_button->doublePressTime = p_configData->doublePressTime * 100u;
	p_button->longPressFunc1 = p_configData->longPressFunc1;
	p_button->longPressTime1 = p_configData->longPressTime1 * 100u;
	p_button->longPressFunc2 = p_configData->longPressFunc2;
	p_button->longPressTime2 = p_configData->longPressTime2 * 100u;
}


// ****************************************************************************
/*!
 * BUTTON_GetEventFunc gets the associated function enumerator to a button event.
 * In the case that the button does not have an active event BUTTON_EVENT_NO_FUNC
 * is returned. There are a few preset system tasks that are handled by this firmware
 * directly or the pijuice service running on the host will respond accordingly.
 * The caller is expected to check for function validity.
 *
 * @param	p_button			pointer to button with active event
 * @retval	ButtonFunction_T	function to perform directly or number to be passed
 * 								to pijuice service routine.
 */
// ****************************************************************************
static ButtonFunction_T BUTTON_GetEventFunc(Button_T * const p_button)
{
	switch (p_button->event)
	{
	case BUTTON_EVENT_PRESS:
		return p_button->pressFunc;

	case BUTTON_EVENT_RELEASE:
		return p_button->releaseFunc;

	case BUTTON_EVENT_SINGLE_PRESS:
		return p_button->singlePressFunc;

	case BUTTON_EVENT_DOUBLE_PRESS:
		return p_button->doublePressFunc;

	case BUTTON_EVENT_LONG_PRESS1:
		return p_button->longPressFunc1;

	case BUTTON_EVENT_LONG_PRESS2:
		return p_button->longPressFunc2;

	default:
		return BUTTON_EVENT_NO_FUNC;
	}
}


// ****************************************************************************
/*!
 * BUTTON_WriteConfigToNv commits configuration data for an indexed button to non
 * volatile memory.
 *
 * @param	p_configData		pointer to a button configuration
 * @param	buttonIdx			index of the button the configuration is for
 * @retval	none
 */
// ****************************************************************************
static void BUTTON_WriteConfigToNv(const Button_T * const p_configData,
									const uint8_t buttonIdx)
{
	const uint8_t nvOffset = BUTTON_PRESS_FUNC_SW1 +
								(m_writebuttonConfigData * (BUTTON_PRESS_FUNC_SW2 - BUTTON_PRESS_FUNC_SW1));

	NV_WriteVariable_U8(nvOffset, p_configData->pressFunc);
	NV_WriteVariable_U8(nvOffset + 2u, p_configData->releaseFunc);
	NV_WriteVariable_U8(nvOffset + 4u, p_configData->singlePressFunc);
	NV_WriteVariable_U8(nvOffset + 5u, p_configData->singlePressTime);
	NV_WriteVariable_U8(nvOffset + 6u, p_configData->doublePressFunc);
	NV_WriteVariable_U8(nvOffset + 7u, p_configData->doublePressTime);
	NV_WriteVariable_U8(nvOffset + 8u, p_configData->longPressFunc1);
	NV_WriteVariable_U8(nvOffset + 9u, p_configData->longPressTime1);
	NV_WriteVariable_U8(nvOffset + 10u, p_configData->longPressFunc2);
	NV_WriteVariable_U8(nvOffset + 11u, p_configData->longPressTime2);
}


// TODO - make these callback assignable
__weak void BUTTON_PowerOnEventCb(const Button_T * const p_button)
{
	UNUSED(p_button);
}

__weak void BUTTON_PowerOffEventCb(const Button_T * const p_button)
{
	UNUSED(p_button);
}

__weak void BUTTON_PowerResetEventCb(const Button_T * const p_button)
{
	UNUSED(p_button);
}

__weak void BUTTON_DualLongPressEventCb(void)
{

}

