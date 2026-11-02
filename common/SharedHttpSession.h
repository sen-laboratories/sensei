/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <private/netservices2/HttpSession.h>

/**
 * The HTTP session of the process: made when it is first asked for and never destroyed.
 *
 * Haiku's BHttpSession crashes with a double free when it is deleted right after an answer was read (a destruction within a few milliseconds of the
 * end of the last request, in about every second try: the thread that handles the connections is still cleaning up the finished request). A plugin
 * is a short-lived process, so the session lives until it exits; it is also shared by everything in the process that asks the network, which
 * needs no more threads than one session has. BHttpSession is safe to use from several threads.
 */
namespace sen {

inline BPrivate::Network::BHttpSession*
SharedHttpSession()
{
	static BPrivate::Network::BHttpSession* session = new BPrivate::Network::BHttpSession();
	return session;
}

}	// namespace sen
