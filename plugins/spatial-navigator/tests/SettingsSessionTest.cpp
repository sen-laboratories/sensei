/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// The settings protocol of the spatial navigator (common/SettingsSession.h, SpatialSettingsForm.cpp), as the settings window of SEN uses it: the plugin is started with the request, answers
// to the messenger in it, takes changes into its settings file and ends with the session. Haiku only; needs the built spatial navigator (bin/) and changes the settings file of
// the navigator for a moment (it is put back):
//   g++ -std=c++20 -Wall -Wno-multichar -I/boot/home/config/non-packaged/develop/headers tests/SettingsSessionTest.cpp -o /tmp/settings-session-test -lbe && /tmp/settings-session-test bin/<the program>

#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Handler.h>
#include <Looper.h>
#include <Path.h>
#include <Roster.h>

#include <stdio.h>
#include <string.h>
#include <string>

#include <sen/Sen.h>
#include <sen/Sensei.h>
#include <sen/SenSettingsForm.h>

namespace form = sen::settings::form;

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

/** the window: it gets the answers that are sent to it */
class Receiver : public BLooper {
public:
	Receiver() : BLooper("receiver"), got(0) {}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == sensei::cmd::kSettingsReply) {
			reply = *message;
			session = message->ReturnAddress();
			got++;
		} else
			BLooper::MessageReceived(message);
	}

	BMessage	reply;
	BMessenger	session;
	int32		got;
};

static bool
WaitFor(Receiver* receiver, int32 count, int seconds = 15)
{
	for (int i = 0; i < seconds * 10 && receiver->got < count; i++)
		snooze(100000);
	return receiver->got >= count;
}

static bool
Gone(const BMessenger& messenger)
{
	for (int i = 0; i < 160; i++) {
		if (!messenger.IsValid())
			return true;
		snooze(100000);
	}
	return false;
}

static std::string
ReadFile(const BPath& path)
{
	BFile file(path.Path(), B_READ_ONLY);
	off_t size = 0;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK)
		return "<none>";
	std::string text(size, '\0');
	file.Read(&text[0], size);
	return text;
}

static void
WriteFile(const BPath& path, const std::string& text)
{
	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	file.Write(text.data(), text.size());
}

int main(int argc, char** argv)
{
	BApplication app("application/x-vnd.sen-labs.spatial-settings-test");
	if (argc < 2) {
		printf("usage: settings-session-test <the program of the spatial navigator>\n");
		return 1;
	}
	entry_ref plugin;
	CHECK(get_ref_for_path(argv[1], &plugin) == B_OK);

	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	path.Append("sen");
	create_directory(path.Path(), 0755);
	path.Append("spatial-navigator.settings");
	const std::string original = ReadFile(path);
	// the status of the servers that a test leaves in the cache: put back as it was
	BPath statusPath;
	find_directory(B_USER_CACHE_DIRECTORY, &statusPath, true);
	statusPath.Append("sen/spatial-navigator/server-status");
	const std::string originalStatus = ReadFile(statusPath);
	WriteFile(path, "# my settings\noverpass_mirror = vk-maps\nunknown = kept\n");

	Receiver* receiver = new Receiver();
	receiver->Run();

	BMessage ask(sensei::cmd::kSettings);
	ask.AddMessenger(sensei::key::kReplyTo, BMessenger(receiver));
	ask.AddInt32("serial", 5);
	CHECK(be_roster->Launch(&plugin, &ask) == B_OK);
	CHECK(WaitFor(receiver, 1));
	CHECK(receiver->session.IsValid());

	BMessage settings, schema, defaults;
	CHECK(receiver->reply.GetInt32(form::kFieldResult, -1) == B_OK);
	CHECK(strcmp(receiver->reply.GetString(sensei::key::kPlugin, ""), "application/x-vnd.sen-plugin.sen-labs.spatial-navigator") == 0);
	CHECK(receiver->reply.FindMessage(form::kFieldSettings, &settings) == B_OK);
	CHECK(receiver->reply.FindMessage(form::kFieldSchema, &schema) == B_OK);
	CHECK(receiver->reply.FindMessage(form::kFieldDefaults, &defaults) == B_OK);
	// a file from before the list (`overpass_mirror = vk-maps`): the list has that server first, and it is the only one that is on
	BMessage rowFirst, rowSecond, defaultFirst;
	CHECK(settings.FindMessage("overpass_servers", 0, &rowFirst) == B_OK && strcmp(rowFirst.GetString("id", ""), "vk-maps") == 0 && rowFirst.GetBool("enabled", false));
	CHECK(settings.FindMessage("overpass_servers", 1, &rowSecond) == B_OK && strcmp(rowSecond.GetString("id", ""), "default") == 0 && !rowSecond.GetBool("enabled", true));
	CHECK(strcmp(rowFirst.GetString("name", ""), "VK Maps") == 0 && strlen(rowFirst.GetString("info", "")) > 10);
	CHECK(defaults.FindMessage("overpass_servers", 0, &defaultFirst) == B_OK && strcmp(defaultFirst.GetString("id", ""), "default") == 0 && defaultFirst.GetBool("enabled", false));
	CHECK(!settings.HasMessage("overpass_servers", 6));

	// the servers are a table (a switch, the name, the description; the rows can be moved); the key is a secret field that follows the selected row; the address of the custom mirror stays
	BMessage radius, limit;
	CHECK(schema.FindMessage("poi_radius", &radius) == B_OK && radius.GetDouble(form::kAttrMin, 0) == 50 && radius.GetDouble(form::kAttrMax, 0) == 1500);
	CHECK(schema.FindMessage("poi_limit", &limit) == B_OK && limit.GetDouble(form::kAttrMin, 0) == 5 && limit.GetDouble(form::kAttrMax, 0) == 200);
	CHECK(settings.GetInt32("poi_radius", 0) == 250 && settings.GetInt32("poi_limit", 0) == 40);
	BMessage cacheHours;
	CHECK(schema.FindMessage("poi_cache_hours", &cacheHours) == B_OK && cacheHours.GetDouble(form::kAttrMin, 0) == 1 && cacheHours.GetDouble(form::kAttrMax, 0) == 168);
	CHECK(settings.GetInt32("poi_cache_hours", 0) == 24);
	BMessage mapStyle;
	CHECK(schema.FindMessage("map_style", &mapStyle) == B_OK && strcmp(mapStyle.GetString(form::kAttrKind, ""), form::kKindChoice) == 0);
	CHECK(strcmp(mapStyle.GetString(form::kAttrChoices, 0, ""), "topo") == 0 && mapStyle.HasString(form::kAttrChoices, 3) && !mapStyle.HasString(form::kAttrChoices, 4));
	CHECK(strcmp(settings.GetString("map_style", ""), "topo") == 0);
	BMessage servers, enabledColumn, idColumn, keyColumn, needsKeyColumn, address, revert, needsKeyField, testButton, key;
	CHECK(schema.FindMessage("overpass_servers", &servers) == B_OK && strcmp(servers.GetString(form::kAttrKind, ""), form::kKindTable) == 0 && servers.GetBool(form::kAttrReorderable, false));
	CHECK(schema.FindMessage("overpass_servers.enabled", &enabledColumn) == B_OK && schema.FindMessage("overpass_servers.id", &idColumn) == B_OK && idColumn.GetBool(form::kAttrHidden, false));
	CHECK(schema.FindMessage("overpass_servers.status", &keyColumn) == B_OK && strcmp(keyColumn.GetString(form::kAttrStateField, ""), "status_state") == 0
		&& strcmp(keyColumn.GetString(form::kAttrDetailField, ""), "status_detail") == 0 && strcmp(keyColumn.GetString(form::kAttrBusyField, ""), "testing") == 0);
	CHECK(strcmp(servers.GetString(form::kAttrUpdateMsg, ""), "OVrw") == 0);		// (the table sends a message when another row is selected)
	// the columns: Use, Server, Description, API key? (a check box), Status
	CHECK(schema.FindMessage("overpass_servers.needs_key", &needsKeyColumn) == B_OK && strcmp(needsKeyColumn.GetString(form::kAttrLabel, ""), "API key?") == 0 && needsKeyColumn.GetBool(form::kAttrReadOnly, false));		// (shown only: the flag is changed in the details)
	// the details of the selected row, from the top: the address (with its revert button), whether the server needs a key, the key: each listens to the message of the table
	CHECK(schema.FindMessage("overpass_address", &address) == B_OK && strcmp(address.GetString(form::kAttrLabel, ""), "Server address") == 0 && strcmp(address.GetString(form::kAttrBindToUpdateMsg, ""), "OVrw") == 0);
	CHECK(schema.FindMessage("overpass_address_revert", &revert) == B_OK && strcmp(revert.GetString(form::kAttrKind, ""), form::kKindButton) == 0 && strcmp(revert.GetString(form::kAttrBeside, ""), "overpass_address") == 0
		&& strcmp(revert.GetString(form::kAttrFieldId, ""), "overpass_address") == 0 && strcmp(revert.GetString(form::kAttrBindToUpdateMsg, ""), "OVrw") == 0);
	CHECK(schema.FindMessage("overpass_needs_key", &needsKeyField) == B_OK && strcmp(needsKeyField.GetString(form::kAttrLabel, ""), "Needs an API key") == 0
		&& strcmp(needsKeyField.GetString(form::kAttrBindToUpdateMsg, ""), "OVrw") == 0);
	CHECK(schema.FindMessage("overpass_test", &testButton) == B_OK && strcmp(testButton.GetString(form::kAttrLabel, ""), "Test availability") == 0 && strcmp(testButton.GetString(form::kAttrBeside, ""), "overpass_servers") == 0);
	CHECK(schema.FindMessage("overpass_key", &key) == B_OK && strcmp(key.GetString(form::kAttrKind, ""), form::kKindSecret) == 0
		&& strcmp(key.GetString(form::kAttrBindToUpdateMsg, ""), "OVrw") == 0);		// (the field listens to it: it does not know the table)
	CHECK(address.GetInt32(form::kAttrOrder, -1) < needsKeyField.GetInt32(form::kAttrOrder, -1) && needsKeyField.GetInt32(form::kAttrOrder, -1) < key.GetInt32(form::kAttrOrder, -1));		// (the address, the flag, the key)
	CHECK(strcmp(key.GetString(form::kAttrBeside, ""), "overpass_needs_key") == 0);		// (the key is in the line of the check box)
	// the key of a server is a message of its row: its value, and whether the server needs a key at all (VK Maps does not, NextGIS does)
	BMessage keyPart, nextgisRow, nextgisPart;
	CHECK(rowFirst.FindMessage("overpass_key", &keyPart) == B_OK && !keyPart.GetBool(form::kUpdateEnabled, true) && strcmp(keyPart.GetString(form::kUpdateValue, "x"), "") == 0);
	CHECK(settings.FindMessage("overpass_servers", 3, &nextgisRow) == B_OK && strcmp(nextgisRow.GetString("id", ""), "nextgis") == 0 && nextgisRow.FindMessage("overpass_key", &nextgisPart) == B_OK
		&& nextgisPart.GetBool(form::kUpdateEnabled, false));
	CHECK(strlen(rowFirst.GetString("status", "")) > 0 && strcmp(rowFirst.GetString("status_state", "x"), "x") != 0);			// (the status is there: "Not tested" or what the last test found)
	// the other parts of the row: the address (and the one of the list), and the flag (NextGIS has a place for the key in its address: the flag cannot be changed)
	BMessage addressPart, flagPart, nextgisFlag;
	CHECK(rowFirst.FindMessage("overpass_address", &addressPart) == B_OK && strcmp(addressPart.GetString(form::kUpdateValue, ""), "https://maps.mail.ru/osm/tools/overpass/api/interpreter") == 0
		&& strcmp(addressPart.GetString(form::kUpdateDefault, "x"), addressPart.GetString(form::kUpdateValue, "y")) == 0);
	CHECK(rowFirst.FindMessage("overpass_needs_key", &flagPart) == B_OK && !flagPart.GetBool(form::kUpdateValue, true) && flagPart.GetBool(form::kUpdateEnabled, true));		// (the check box can always be used)
	CHECK(nextgisRow.FindMessage("overpass_needs_key", &nextgisFlag) == B_OK && nextgisFlag.GetBool(form::kUpdateValue, false) && nextgisFlag.GetBool(form::kUpdateEnabled, true));
	CHECK(rowFirst.GetBool("needs_key", true) == false && nextgisRow.GetBool("needs_key", false));
	CHECK(!settings.HasString("overpass_url") && !settings.HasBool("overpass_custom_key") && settings.HasBool("overpass_test"));		// (the address and the flag are cells of the rows now)
	CHECK(strcmp(rowFirst.GetString("status", ""), "n/a") == 0 || strcmp(rowFirst.GetString("status_state", ""), "") != 0);		// (a status is a test result, or n/a)

	// the cells of the list: the key of the first row (VK Maps), the switch of the second (the public server), and the other fields
	BMessage change;
	change.AddString("overpass_servers_03.overpass_key.value", "k3y");		// (the key of NextGIS, which is off: no test)
	change.AddBool("overpass_servers_01.enabled", true);
	change.AddString("overpass_servers_05.overpass_address.value", "https://overpass.example.org/api/interpreter");		// (the custom mirror: its address, and that it wants a key)
	change.AddBool("overpass_servers_05.needs_key", true);
	change.AddString("overpass_servers_02.overpass_address.value", "https://mirror.example.org/api/interpreter");		// (Private.coffee: another address than the one of the list)
	change.AddInt32("poi_radius", 400);
	BMessage set(sensei::cmd::kSettingsSet), answer;
	set.AddMessage(form::kFieldSettings, &change);
	CHECK(receiver->session.SendMessage(&set, &answer, 5000000, 5000000) == B_OK);
	CHECK(answer.GetInt32(form::kFieldResult, -1) == B_OK);
	// the server that was switched on is tested in the background: the answer already says so (or has the result, if the server answered at once)
	BMessage afterSettings, defaultRow;
	CHECK(answer.FindMessage(form::kFieldSettings, &afterSettings) == B_OK && afterSettings.FindMessage("overpass_servers", 1, &defaultRow) == B_OK
		&& strcmp(defaultRow.GetString("id", ""), "default") == 0 && defaultRow.GetBool("enabled", false) && strlen(defaultRow.GetString("status", "")) > 0);
	// the test is done after a while (it asks the server over the network): the plugin sends the settings to the window again, with the status of the server in its row
	CHECK(WaitFor(receiver, 2, 40));
	BMessage pushed, pushedRow;
	CHECK(receiver->reply.FindMessage(form::kFieldSettings, &pushed) == B_OK && pushed.FindMessage("overpass_servers", 1, &pushedRow) == B_OK && strcmp(pushedRow.GetString("id", ""), "default") == 0
		&& !pushedRow.GetBool("testing", true) && (strcmp(pushedRow.GetString("status_state", ""), "ok") == 0 || strcmp(pushedRow.GetString("status_state", ""), "warning") == 0
			|| strcmp(pushedRow.GetString("status_state", ""), "error") == 0) && strlen(pushedRow.GetString("status_detail", "")) > 0);
	std::string file = ReadFile(path);
	CHECK(file.find("# my settings\n") != std::string::npos && file.find("unknown = kept\n") != std::string::npos);
	CHECK(file.find("overpass_servers = vk-maps default -private-coffee -nextgis -overpass-ch -custom\n") != std::string::npos);
	CHECK(file.find("overpass_key_nextgis = k3y\n") != std::string::npos && file.find("overpass_url = https://overpass.example.org/api/interpreter\n") != std::string::npos);
	CHECK(file.find("poi_radius = 400\n") != std::string::npos && file.find("overpass_custom_key = yes\n") != std::string::npos);
	CHECK(file.find("overpass_url_private_coffee = https://mirror.example.org/api/interpreter\n") != std::string::npos);
	// the address of the list again (what Revert does): nothing is stored for it, and the row shows the address of the list
	BMessage back;
	back.AddString("overpass_servers_02.overpass_address.value", "https://overpass.private.coffee/api/interpreter");
	BMessage setBack(sensei::cmd::kSettingsSet);
	setBack.AddMessage(form::kFieldSettings, &back);
	BMessage backAnswer, backSettings, backRow, backPart;
	CHECK(receiver->session.SendMessage(&setBack, &backAnswer, 5000000, 5000000) == B_OK && backAnswer.GetInt32(form::kFieldResult, -1) == B_OK);
	file = ReadFile(path);
	CHECK(file.find("overpass_url_private_coffee = \n") != std::string::npos && file.find("mirror.example.org") == std::string::npos);
	CHECK(backAnswer.FindMessage(form::kFieldSettings, &backSettings) == B_OK && backSettings.FindMessage("overpass_servers", 2, &backRow) == B_OK && backRow.FindMessage("overpass_address", &backPart) == B_OK
		&& strcmp(backPart.GetString(form::kUpdateValue, ""), "https://overpass.private.coffee/api/interpreter") == 0);
	// the check box of the details and the one of the column are the same flag
	BMessage flagChange;
	flagChange.AddBool("overpass_servers_01.overpass_needs_key.value", true);
	BMessage setFlag(sensei::cmd::kSettingsSet), flagAnswer;
	setFlag.AddMessage(form::kFieldSettings, &flagChange);
	CHECK(receiver->session.SendMessage(&setFlag, &flagAnswer, 5000000, 5000000) == B_OK && flagAnswer.GetInt32(form::kFieldResult, -1) == B_OK);
	file = ReadFile(path);
	CHECK(file.find("overpass_needs_key_default = yes\n") != std::string::npos);
	BMessage flagSettings, flagRow;
	CHECK(flagAnswer.FindMessage(form::kFieldSettings, &flagSettings) == B_OK && flagSettings.FindMessage("overpass_servers", 1, &flagRow) == B_OK && flagRow.GetBool("needs_key", false)
		&& strcmp(flagRow.GetString("status", ""), "n/a") == 0);		// (it is on and now needs a key that it has not: no test, n/a)
	flagChange.MakeEmpty();
	flagChange.AddBool("overpass_servers_01.needs_key", false);
	setFlag.MakeEmpty();
	setFlag.what = sensei::cmd::kSettingsSet;
	setFlag.AddMessage(form::kFieldSettings, &flagChange);
	CHECK(receiver->session.SendMessage(&setFlag, &flagAnswer, 5000000, 5000000) == B_OK && flagAnswer.GetInt32(form::kFieldResult, -1) == B_OK);
	file = ReadFile(path);
	CHECK(file.find("overpass_needs_key_default = \n") != std::string::npos);
	// (that change tests the public server again: wait for it, so that the button below starts the tests of both servers that are on)
	CHECK(WaitFor(receiver, 3, 45));
	// the button "Test availability" asks every server that is on: the rows say that they are under way, and each pushes its result
	BMessage press;
	press.AddBool("overpass_test", true);
	BMessage setPress(sensei::cmd::kSettingsSet), pressAnswer, pressSettings, testingFirst, testingSecond;
	setPress.AddMessage(form::kFieldSettings, &press);
	CHECK(receiver->session.SendMessage(&setPress, &pressAnswer, 5000000, 5000000) == B_OK && pressAnswer.GetInt32(form::kFieldResult, -1) == B_OK);
	CHECK(pressAnswer.FindMessage(form::kFieldSettings, &pressSettings) == B_OK && pressSettings.FindMessage("overpass_servers", 0, &testingFirst) == B_OK && pressSettings.FindMessage("overpass_servers", 1, &testingSecond) == B_OK
		&& strcmp(testingFirst.GetString("id", ""), "vk-maps") == 0 && testingFirst.GetBool("testing", false) && strcmp(testingSecond.GetString("id", ""), "default") == 0 && testingSecond.GetBool("testing", false));
	CHECK(WaitFor(receiver, 5, 60));

	// a row moved (the whole table, in the new order): the order is written, the switches stay
	BMessage moved;
	for (const char* id : {"default", "vk-maps", "private-coffee", "nextgis", "overpass-ch", "custom"}) {
		BMessage row;
		row.AddString("id", id);
		row.AddBool("enabled", strcmp(id, "default") == 0 || strcmp(id, "vk-maps") == 0);
		moved.AddMessage("overpass_servers", &row);
	}
	BMessage setMoved(sensei::cmd::kSettingsSet);
	setMoved.AddMessage(form::kFieldSettings, &moved);
	answer.MakeEmpty();
	CHECK(receiver->session.SendMessage(&setMoved, &answer, 5000000, 5000000) == B_OK && answer.GetInt32(form::kFieldResult, -1) == B_OK);
	file = ReadFile(path);
	CHECK(file.find("overpass_servers = default vk-maps -private-coffee -nextgis -overpass-ch -custom\n") != std::string::npos);

	BMessage bad;
	bad.AddString("overpass_servers_09.enabled", "true");
	bad.AddString("overpass_url", "not an address");
	BMessage setBad(sensei::cmd::kSettingsSet);
	setBad.AddMessage(form::kFieldSettings, &bad);
	answer.MakeEmpty();
	CHECK(receiver->session.SendMessage(&setBad, &answer, 5000000, 5000000) == B_OK);
	CHECK(answer.GetInt32(form::kFieldResult, B_OK) != B_OK);
	const std::string detail = answer.GetString(sen::key::kDetail, "");
	CHECK(detail.find("overpass_servers_09") != std::string::npos && detail.find("overpass_url") != std::string::npos);
	BMessage now, firstNow;
	CHECK(answer.FindMessage(form::kFieldSettings, &now) == B_OK && now.FindMessage("overpass_servers", 0, &firstNow) == B_OK && strcmp(firstNow.GetString("id", ""), "default") == 0);

	BMessenger session = receiver->session;
	session.SendMessage(sensei::cmd::kSettingsClose);
	CHECK(Gone(session));

	WriteFile(statusPath, originalStatus == "<none>" ? "" : originalStatus);
	if (originalStatus == "<none>")
		BEntry(statusPath.Path()).Remove();
	WriteFile(path, original == "<none>" ? "" : original);
	if (original == "<none>")
		BEntry(path.Path()).Remove();
	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
