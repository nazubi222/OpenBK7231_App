#include "../new_common.h"
#include "../new_pins.h"
#include "../new_cfg.h"
// Commands register, execution API and cmd tokenizer
#include "../cmnds/cmd_public.h"
#include "../mqtt/new_mqtt.h"
#include "../logging/logging.h"
#include "../hal/hal_pins.h"
#include "drv_public.h"
#include "drv_local.h"
#include "drv_uart.h"
#include "../httpserver/new_http.h"

/*
PWM toggler provides you an abstraction layer over PWM channels 
and allows you to enable/disable them without losing the set PWM value.

PWM toggler was created to support lamp with RGB LED and PWM laser and PWM motor.

Configuration:
- set channels 1, 2 and 3 for RGB 
- enable "force RGB mode" in General/Flags
- set channels 4 for motor and 5 for laser
- on web panel, create autoexec.bat with following script:

	// enable toggler driver
	startDriver PWMToggler
	// toggler slot 0 will be channel 4
	toggler_channel0 4
	// toggler slot 0 display name is laser
	toggler_name0 Laser

	// toggler slot 1 will be channel 5
	toggler_channel1 5
	// toggler slot 1 display name is motor
	toggler_name1 Motor

- for HA, configure RGB as usual in Yaml
- also add yaml entries for laser and motor:

	 - unique_id: "OpenBK7231T_760BF030_galaxy_laser"
		name: "GenioLaser"
		command_topic: "cmnd/obk760BF030/toggler_enable0"
		state_topic: "obk760BF030/toggler_enable0/get"
		payload_on: 1
		payload_off: 0
		brightness_command_topic: "cmnd/obk760BF030/toggler_set0"
		brightness_scale: 100
		brightness_state_topic: "obk760BF030/toggler_set0/get"
		brightness_value_template: "{{value}}"
		qos: 1





*/

#define MAX_ONOFF_SLOTS 6

static char *g_names[MAX_ONOFF_SLOTS] = { 0 };
static int g_channels[MAX_ONOFF_SLOTS];
static int g_values[MAX_ONOFF_SLOTS];
static bool g_enabled[MAX_ONOFF_SLOTS];

int parsePowerArgument(const char *s);

void publish_enableState(int index) {
	char topic[32];
	snprintf(topic, sizeof(topic), "toggler_enable%i", index);

	MQTT_PublishMain_StringInt(topic, g_enabled[index]);
}
void publish_value(int index) {
	char topic[32];
	snprintf(topic, sizeof(topic), "toggler_set%i", index);

	MQTT_PublishMain_StringInt(topic, g_values[index]);
}
void apply(int index) {
	if (g_enabled[index]) {
		CHANNEL_Set(g_channels[index], g_values[index], 0);
	}
	else {
		CHANNEL_Set(g_channels[index], 0, 0);
	}
	publish_enableState(index);
	publish_value(index);
}
void Toggler_Set(int index, int value) {
	g_values[index] = value;
	apply(index);
}
void Toggler_Toggle(int index) {
	g_enabled[index] = !g_enabled[index];
	apply(index);
}
commandResult_t Toggler_NameX(const void *context, const char *cmd, const char *args, int cmdFlags) {
	const char *indexStr;
	int index;
	bool bEnabled;

	if (args == 0 || *args == 0) {
		addLogAdv(LOG_INFO, LOG_FEATURE_ENERGYMETER, "This command needs one argument");
		return CMD_RES_NOT_ENOUGH_ARGUMENTS;
	}

	indexStr = cmd + strlen("toggler_name");
	index = atoi(indexStr);

	if (g_names[index])
		free(g_names[index]);
	g_names[index] = strdup(args);

	return CMD_RES_OK;
}
commandResult_t Toggler_EnableX(const void *context, const char *cmd, const char *args, int cmdFlags) {
	const char *indexStr;
	int index;
	bool bEnabled;

	if (args == 0 || *args == 0) {
		addLogAdv(LOG_INFO, LOG_FEATURE_ENERGYMETER, "This command needs one argument");
		return CMD_RES_NOT_ENOUGH_ARGUMENTS;
	}

	indexStr = cmd + strlen("toggler_enable");
	index = atoi(indexStr);

	bEnabled = parsePowerArgument(args);
	g_enabled[index] = bEnabled;

	apply(index);


	return CMD_RES_OK;
}

commandResult_t Toggler_SetX(const void *context, const char *cmd, const char *args, int cmdFlags) {
	const char *indexStr;
	int index;
	bool bEnabled;

	if (args == 0 || *args == 0) {
		addLogAdv(LOG_INFO, LOG_FEATURE_ENERGYMETER, "This command needs one argument");
		return CMD_RES_NOT_ENOUGH_ARGUMENTS;
	}

	indexStr = cmd + strlen("toggler_set");
	index = atoi(indexStr);

	g_values[index] = atoi(args);

	apply(index);


	return CMD_RES_OK;
}
commandResult_t Toggler_ChannelX(const void *context, const char *cmd, const char *args, int cmdFlags) {
	const char *indexStr;
	int index;
	bool bEnabled;

	if (args == 0 || *args == 0) {
		addLogAdv(LOG_INFO, LOG_FEATURE_ENERGYMETER, "This command needs one argument");
		return CMD_RES_NOT_ENOUGH_ARGUMENTS;
	}

	indexStr = cmd + strlen("toggler_channel");
	index = atoi(indexStr);

	g_channels[index] = atoi(args);

	return CMD_RES_OK;
}
const char *Toggler_GetName(int i) {
	const char *name = g_names[i];
	if (name == 0)
		name = "Unnamed";
	return name;
}
void DRV_Toggler_ProcessChanges(http_request_t *request) {
	int j;
	int val;
	char tmpA[8];

	if (http_getArg(request->url, "togglerOn", tmpA, sizeof(tmpA))) {
		j = atoi(tmpA);
		const char *name = Toggler_GetName(j);
		hprintf255(request, "<h3>Toggled %s!</h3>", name);
		Toggler_Toggle(j);
	}
	if (http_getArg(request->url, "togglerValueID", tmpA, sizeof(tmpA))) {
		j = atoi(tmpA);
		const char *name = Toggler_GetName(j);
		http_getArg(request->url, "togglerValue", tmpA, sizeof(tmpA));
		val = atoi(tmpA);
		Toggler_Set(j, val);
		hprintf255(request, "<h3>Set value %i for %s!</h3>", val, name);
	}

}
void DRV_Toggler_AddToHtmlPage(http_request_t *request) {
	int i;
	const char *c;

	for (i = 0; i < MAX_ONOFF_SLOTS; i++) {
		const char *name = Toggler_GetName(i);
		int maxValue = 100;
		if (g_channels[i] == -1)
			continue;

		//hprintf255(request, "<tr> Toggler %s</tr>",name);

		hprintf255(request, "<tr>");
		if (g_enabled[i]) {
			c = "bgrn";
		}
		else {
			c = "bred";
		}
		poststr(request, "<td><form action=\"index\">");
		hprintf255(request, "<input type=\"hidden\" name=\"togglerOn\" value=\"%i\">", i);
		hprintf255(request, "<input class=\"%s\" type=\"submit\" value=\"Toggle %s\"/></form></td>", c, name);
		poststr(request, "</tr>");

		poststr(request, "<tr><td>");
		hprintf255(request, "<form action=\"index\" id=\"form%i\">", i);
		hprintf255(request, "<input type=\"range\" min=\"0\" max=\"%i\" name=\"togglerValue\" id=\"togglerValue\" value=\"%i\" onchange=\"this.form.submit()\">", 
			maxValue, g_values[i]);
		hprintf255(request, "<input type=\"hidden\" name=\"togglerValueID\" value=\"%i\">", i);
		hprintf255(request, "<input type=\"submit\" style=\"display:none;\" value=\"Set %s\"/></form>", name);
		poststr(request, "</td></tr>");

	}
}
void DRV_Toggler_AppendInformationToHTTPIndexPage(http_request_t* request) {
	int i;
	hprintf255(request, "<h4>Toggler: ");
	int cnt = 0;
	for (i = 0; i < MAX_ONOFF_SLOTS; i++) {
		if (g_channels[i] == -1)
			continue;
		if (cnt != 0) {
			hprintf255(request, ", ");
		}
		const char *name = Toggler_GetName(i);
		const char *st;
		if (g_enabled[i])
			st = "ON";
		else
			st = "OFF";
		hprintf255(request, "slot %i-%s (target %i) has value %i, state %s", 
			i, name, g_channels[i], g_values[i],st);
		cnt++;
	}
	hprintf255(request, "</h4>");

}
void DRV_InitPWMToggler() {
	int i;

	for (i = 0; i < MAX_ONOFF_SLOTS; i++) {
		g_channels[i] = -1;
		g_names[i] = 0;
	}
	// sets the given output ON or OFF
	// handles toggler_enable0, toggler_enable1, etc
	CMD_RegisterCommand("toggler_enable", "", Toggler_EnableX, NULL, NULL);
	// sets the VALUE of given output
	// handles toggler_set0, toggler_set1, etc
	CMD_RegisterCommand("toggler_set", "", Toggler_SetX, NULL, NULL);
	// handles toggler_channel0, toggler_channel1
	CMD_RegisterCommand("toggler_channel", "", Toggler_ChannelX, NULL, NULL);
	// handles toggler_name0 etc
	CMD_RegisterCommand("toggler_name", "", Toggler_NameX, NULL, NULL);
}






