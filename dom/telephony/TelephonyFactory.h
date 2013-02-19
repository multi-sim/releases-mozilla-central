/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=40: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_telephony_telephonyfactory_h__
#define mozilla_dom_telephony_telephonyfactory_h__

#include "nsIDOMTelephonyManager.h"
#include "nsIDOMVoicemailManager.h"
#include "nsPIDOMWindow.h"
// Implemented in TelephonyManager.cpp / VoicemailManager.cpp.
nsresult
NS_NewTelephonyManager(nsPIDOMWindow* aWindow, nsIDOMTelephonyManager** aManager);

nsresult
NS_NewVoicemailManager(nsPIDOMWindow* aWindow, nsIDOMMozVoicemailManager** aManager);

#endif // mozilla_dom_telephony_telephonyfactory_h__
