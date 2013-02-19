/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=40: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_telephony_voicemailmanager_h__
#define mozilla_dom_telephony_voicemailmanager_h__

#include "TelephonyCommon.h"
#include "nsDOMEventTargetHelper.h"
#include "nsIDOMVoicemailManager.h"
#include "Voicemail.h"

class nsPIDOMWindow;

BEGIN_TELEPHONY_NAMESPACE

class VoicemailManager : public nsDOMEventTargetHelper
                       , public nsIDOMMozVoicemailManager
{
  nsTArray<nsRefPtr<Voicemail> > mVoicemails;
  bool mRooted;

public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMMOZVOICEMAILMANAGER
  NS_FORWARD_NSIDOMEVENTTARGET(nsDOMEventTargetHelper::)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(
                                                   VoicemailManager,
                                                   nsDOMEventTargetHelper)

  static already_AddRefed<VoicemailManager>
  Create(nsPIDOMWindow* aOwner);

private:
  VoicemailManager();
  ~VoicemailManager();
};

END_TELEPHONY_NAMESPACE

#endif // mozilla_dom_telephony_voicemailmanager_h__
