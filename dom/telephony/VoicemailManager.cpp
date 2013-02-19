/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=40: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VoicemailManager.h"
#include "Voicemail.h"
#include "TelephonyFactory.h"

#include "nsIURI.h"
#include "nsIURL.h"
#include "nsPIDOMWindow.h"

#include "jsapi.h"
#include "nsCycleCollectionParticipant.h"
#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsServiceManagerUtils.h"
#include "nsTArrayHelpers.h"

#define PHONE_NUMBER 2

// TODO Determine default phone.
#define DEFAULT_PHONE_INDEX 0

DOMCI_DATA(MozVoicemailManager, mozilla::dom::telephony::VoicemailManager)

USING_TELEPHONY_NAMESPACE

NS_IMPL_CYCLE_COLLECTION_CLASS(VoicemailManager)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(VoicemailManager,
                                                  nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(VoicemailManager,
                                               nsDOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(VoicemailManager,
                                                nsDOMEventTargetHelper)
  tmp->mVoicemails.Clear();

NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(VoicemailManager)
  NS_INTERFACE_MAP_ENTRY(nsIDOMMozVoicemailManager)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(MozVoicemailManager)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(VoicemailManager, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(VoicemailManager, nsDOMEventTargetHelper)

VoicemailManager::VoicemailManager()
: mRooted(false)
{
}

VoicemailManager::~VoicemailManager()
{
  if (mRooted) {
    NS_DROP_JS_OBJECTS(this, VoicemailManager);
  }
}

// static
already_AddRefed<VoicemailManager>
VoicemailManager::Create(nsPIDOMWindow* aOwner)
{
  NS_ASSERTION(aOwner, "Null owner!");

  nsCOMPtr<nsIScriptGlobalObject> sgo = do_QueryInterface(aOwner);
  NS_ENSURE_TRUE(sgo, nullptr);

  nsCOMPtr<nsIScriptContext> scriptContext = sgo->GetContext();
  NS_ENSURE_TRUE(scriptContext, nullptr);

  nsRefPtr<VoicemailManager> manager = new VoicemailManager();

  manager->BindToOwner(aOwner);

  for (uint32_t index = 0; index < PHONE_NUMBER; index++) {
    nsRefPtr<Voicemail> voicemail = Voicemail::Create(aOwner, index);
    NS_ENSURE_TRUE(voicemail, nullptr);

    NS_ASSERTION(!(manager->mVoicemails).Contains(voicemail),
                 "Shouldn't be in the list!");
    (manager->mVoicemails).AppendElement(voicemail);
  }

  return manager.forget();
}

// nsIDOMMozVoicemailManager

NS_IMETHODIMP
VoicemailManager::GetVoicemails(jsval* aVoicemails)
{
  JSObject* voicemails;
  if (!voicemails) {
    nsresult rv;
    nsIScriptContext* sc = GetContextForEventHandlers(&rv);
    NS_ENSURE_SUCCESS(rv, rv);
    if (sc) {
      rv = nsTArrayToJSArray(sc->GetNativeContext(), mVoicemails, &voicemails);
      NS_ENSURE_SUCCESS(rv, rv);

      if (!mRooted) {
        NS_HOLD_JS_OBJECTS(this, VoicemailManager);
        mRooted = true;
      }
    } else {
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  aVoicemails->setObject(*voicemails);
  return NS_OK;
}

NS_IMETHODIMP
VoicemailManager::GetDefaultVoicemail(nsIDOMMozVoicemail** aDefaultVoicemail)
{
  *aDefaultVoicemail = mVoicemails[DEFAULT_PHONE_INDEX];
  return NS_OK;
}

nsresult
NS_NewVoicemailManager(nsPIDOMWindow* aWindow,
                       nsIDOMMozVoicemailManager** aManager)
{
  NS_ASSERTION(aWindow, "Null pointer!");

  nsPIDOMWindow* innerWindow = aWindow->IsInnerWindow() ?
    aWindow :
    aWindow->GetCurrentInnerWindow();

  nsRefPtr<VoicemailManager> manager = VoicemailManager::Create(innerWindow);
  NS_ENSURE_TRUE(manager, NS_ERROR_UNEXPECTED);

  manager.forget(aManager);
  return NS_OK;
}
