/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file SettingsSession.h
 * @brief The plugin side of the settings protocol (sensei::cmd::kSettings, kSettingsSet and kSettingsClose; see the spec in sento, section 6).
 *
 * A plugin has no window for its settings: the settings window of SEN (sen_settings) asks for them, shows them with the form of Shoji, and sends back what the user changes.
 * The plugin keeps and validates its settings itself, in the place and format of its choice; SEN never reads or writes them. What a plugin does:
 *
 *   1. implement SettingsProvider: describe the settings (a message, a schema with labels and ranges, the defaults) and apply changes (validate, store);
 *   2. keep a SettingsSession in the application, hand it the messages in MessageReceived(): `if (fSession.Handle(message, &provider, signature)) ...`;
 *   3. end the application when the session is over (SettingsSession::IsEnded()) or gone quiet (Expired(), checked in Pulse(); SetPulseRate() when a session opens).
 *
 * The window starts the plugin with the request, so a plugin that is started for it has nobody that waits for a reply: it answers to the messenger in `reply_to`
 * (kSettingsReply), which also tells the window which instance of a multiple launch application it is talking to. A plugin that is asked while it runs, with a reply waited
 * for, answers with a reply. Either way the answer has `settings`, `schema`, `defaults` and `result`.
 */

#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>

#include <string>
#include <vector>

#include <sen/Sen.h>
#include <sen/Sensei.h>
#include <sen/SenSettingsForm.h>

namespace sensei {

/** What a plugin tells the settings window about its settings. */
class SettingsProvider {
public:
	virtual					~SettingsProvider() {}

	/** The settings as they are now (a message of the supported field types, see SenSettingsForm.h), and the schema and defaults, which may be left empty. */
	virtual void			Describe(BMessage* settings, BMessage* schema, BMessage* defaults) = 0;

	/**
	 * Changes: every field is a value under its path key (a flat message of fields of the settings; the whole message after a revert or defaults). Validate, apply what is
	 * valid and store it. The names of the fields that are refused are added to @p refused. Returns B_OK if everything was applied, else the first error.
	 */
	virtual status_t		Apply(const BMessage& changes, std::vector<std::string>* refused) = 0;
};


/** The settings session of a plugin with the settings window that asked. */
class SettingsSession {
public:
							SettingsSession() : fOpen(false), fEnded(false), fLastUsed(0) {}

	/** Handles kSettings, kSettingsSet and kSettingsClose; false if the message is none of them. @p signature is that of the plugin. */
	bool					Handle(BMessage* message, SettingsProvider* provider, const char* signature)
	{
		switch (message->what) {
			case cmd::kSettings:
				_Open(message, provider, signature);
				return true;
			case cmd::kSettingsSet:
				_Set(message, provider);
				return true;
			case cmd::kSettingsClose:
				fEnded = true;
				return true;
		}
		return false;
	}

	/** A session was opened (and not closed). */
	bool					IsOpen() const { return fOpen && !fEnded; }
	/** The window said that it is done. */
	bool					IsEnded() const { return fEnded; }
	/** The window that asked is gone, or it has not been heard of for @p idle (a minute): the session is over, the plugin ends. */
	bool					Expired(bigtime_t idle = 60000000) const
	{
		return fOpen && (!fOwner.IsValid() || system_time() - fLastUsed > idle);
	}

private:
	void					_Open(BMessage* message, SettingsProvider* provider, const char* signature)
	{
		fOpen = true;
		fEnded = false;
		fLastUsed = system_time();
		message->FindMessenger(key::kReplyTo, &fOwner);
		if (!fOwner.IsValid())
			fOwner = message->ReturnAddress();

		BMessage settings, schema, defaults;
		provider->Describe(&settings, &schema, &defaults);

		BMessage reply(cmd::kSettingsReply);
		reply.AddInt32(sen::settings::form::kFieldResult, B_OK);
		reply.AddInt32(sen::settings::form::kFieldProtocol, sen::settings::form::kProtocol);
		reply.AddString(key::kPlugin, signature);
		reply.AddMessage(sen::settings::form::kFieldSettings, &settings);
		if (!schema.IsEmpty())
			reply.AddMessage(sen::settings::form::kFieldSchema, &schema);
		if (!defaults.IsEmpty())
			reply.AddMessage(sen::settings::form::kFieldDefaults, &defaults);
		// (the serial of the request, which the window uses to tell the answers apart)
		int32 serial;
		if (message->FindInt32("serial", &serial) == B_OK)
			reply.AddInt32("serial", serial);

		if (message->IsSourceWaiting())
			message->SendReply(&reply);
		else if (fOwner.IsValid())
			fOwner.SendMessage(&reply);
	}

	void					_Set(BMessage* message, SettingsProvider* provider)
	{
		fLastUsed = system_time();
		BMessage changes;
		status_t result = message->FindMessage(sen::settings::form::kFieldSettings, &changes);
		std::vector<std::string> refused;
		if (result == B_OK)
			result = provider->Apply(changes, &refused);

		BMessage settings, schema, defaults;
		provider->Describe(&settings, &schema, &defaults);
		BMessage reply(B_REPLY);
		reply.AddInt32(sen::settings::form::kFieldResult, result);
		if (!refused.empty()) {
			std::string detail = "Refused: ";
			for (size_t i = 0; i < refused.size(); i++)
				detail += (i > 0 ? ", " : "") + refused[i];
			reply.AddString(sen::key::kDetail, detail.c_str());
		}
		reply.AddMessage(sen::settings::form::kFieldSettings, &settings);
		message->SendReply(&reply);
	}

	bool					fOpen;
	bool					fEnded;
	bigtime_t				fLastUsed;
	BMessenger				fOwner;
};

}	// namespace sensei
