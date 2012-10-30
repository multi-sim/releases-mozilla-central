/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=40: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TelephonyManager.h"
#include "TelephonyFactory.h"

#include "nsIURI.h"
#include "nsIURL.h"
#include "nsPIDOMWindow.h"

#include "jsapi.h"
#include "nsIPermissionManager.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"
#include "SystemWorkerManager.h"
#include "nsRadioInterfaceLayer.h"
#include "nsTArrayHelpers.h"

#include "CallEvent.h"
#include "TelephonyCall.h"

#include <android/log.h>
#define LOGI(args...)  __android_log_print(ANDROID_LOG_INFO, "TelephonyManager" , ## args)

#define PHONE_NUMBER 1

USING_TELEPHONY_NAMESPACE
using namespace mozilla::dom::gonk;

TelephonyManager::TelephonyManager()
: mActiveCall(nullptr), mCallsArray(nullptr), mRooted(false)
{
}

TelephonyManager::~TelephonyManager()
{
  if (mRooted) {
    NS_DROP_JS_OBJECTS(this, TelephonyManager);
  }
}

// static
already_AddRefed<TelephonyManager>
TelephonyManager::Create(nsPIDOMWindow* aOwner)
{
  NS_ASSERTION(aOwner, "Null owner!");

  nsCOMPtr<nsIScriptGlobalObject> sgo = do_QueryInterface(aOwner);
  NS_ENSURE_TRUE(sgo, nullptr);

  nsCOMPtr<nsIScriptContext> scriptContext = sgo->GetContext();
  NS_ENSURE_TRUE(scriptContext, nullptr);

  nsRefPtr<TelephonyManager> manager = new TelephonyManager();

  manager->BindToOwner(aOwner);

  for (uint32_t index = 0; index < PHONE_NUMBER; index++) {
    nsCOMPtr<nsIRILContentHelper> ril =
      do_CreateInstance(NS_RILCONTENTHELPER_CONTRACTID);
    NS_ENSURE_TRUE(ril, nullptr);

    nsRefPtr<Telephony> telephony = Telephony::Create(manager, index, ril);
    NS_ENSURE_TRUE(telephony, nullptr);
    
    NS_ASSERTION(!(manager->mPhones).Contains(telephony), "Shouldn't be in the list!");
    (manager->mPhones).AppendElement(telephony);
  }

  manager->mDefaultPhone = manager->mPhones[0];

  return manager.forget();
}

nsresult
TelephonyManager::NotifyCallsChanged(TelephonyCall* aCall)
{
  nsRefPtr<CallEvent> event = CallEvent::Create(aCall);
  NS_ASSERTION(event, "This should never fail!");
/*
  if (aCall->CallState() == nsIRadioInterfaceLayer::CALL_STATE_DIALING) {
    mActiveCall = aCall;
  }
*/
  nsresult rv =
    event->Dispatch(ToIDOMEventTarget(), NS_LITERAL_STRING("callschanged"));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(TelephonyManager)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(TelephonyManager,
                                                  nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
  for (uint32_t index = 0; index < tmp->mPhones.Length(); index++) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mPhones[i]");
    cb.NoteXPCOMChild(tmp->mPhones[index]->ToISupports());
  }
  for (uint32_t index = 0; index < tmp->mCalls.Length(); index++) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mCalls[i]");
    cb.NoteXPCOMChild(tmp->mCalls[index]->ToISupports());
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(TelephonyManager,
                                               nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mCallsArray)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(TelephonyManager,
                                                nsDOMEventTargetHelper)
  tmp->mPhones.Clear();
  tmp->mDefaultPhone = nullptr;
  tmp->mCalls.Clear();
  tmp->mActiveCall = nullptr;
  tmp->mCallsArray = nullptr;
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(TelephonyManager)
  NS_INTERFACE_MAP_ENTRY(nsIDOMTelephonyManager)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(TelephonyManager)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(TelephonyManager, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(TelephonyManager, nsDOMEventTargetHelper)

DOMCI_DATA(TelephonyManager, TelephonyManager)

NS_IMETHODIMP
TelephonyManager::GetMuted(bool* aMuted)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
TelephonyManager::SetMuted(bool aMuted)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
TelephonyManager::GetSpeakerEnabled(bool* aSpeakerEnabled)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
TelephonyManager::SetSpeakerEnabled(bool aSpeakerEnabled)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
TelephonyManager::GetActive(jsval* aActive)
{/*
  if (!mActiveCall) {
    aActive->setNull();
    return NS_OK;
  }

  nsresult rv;
  nsIScriptContext* sc = GetContextForEventHandlers(&rv);
  NS_ENSURE_SUCCESS(rv, rv);
  if (sc) {
    rv =
      nsContentUtils::WrapNative(sc->GetNativeContext(),
                                 sc->GetNativeGlobal(),
                                 mActiveCall->ToISupports(), aActive);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
  */
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
TelephonyManager::GetCalls(jsval* aCalls)
{
  JSObject* calls = mCallsArray;
  if (!calls) {
    nsresult rv;
    nsIScriptContext* sc = GetContextForEventHandlers(&rv);
    NS_ENSURE_SUCCESS(rv, rv);
    if (sc) {
      rv = nsTArrayToJSArray(sc->GetNativeContext(), mCalls, &calls);
      NS_ENSURE_SUCCESS(rv, rv);

      if (!mRooted) {
        NS_HOLD_JS_OBJECTS(this, TelephonyManager);
        mRooted = true;
      }

      mCallsArray = calls;
    } else {
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  aCalls->setObject(*calls);
  return NS_OK;
  //return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
TelephonyManager::GetPhoneState(nsAString& aPhoneState)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
TelephonyManager::GetPhones(jsval* aPhones)
{  
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
TelephonyManager::GetDefaultPhone(nsIDOMTelephony** aDefaultPhone)
{
  *aDefaultPhone = mDefaultPhone;
  return NS_OK;
  //return NS_ERROR_NOT_IMPLEMENTED;  
}

NS_IMPL_EVENT_HANDLER(TelephonyManager, incoming)
NS_IMPL_EVENT_HANDLER(TelephonyManager, callschanged)

nsresult
NS_NewTelephonyManager(nsPIDOMWindow* aWindow,
                       nsIDOMTelephonyManager** aManager)
{
  NS_ASSERTION(aWindow, "Null pointer!");

  nsPIDOMWindow* innerWindow = aWindow->IsInnerWindow() ?
    aWindow :
    aWindow->GetCurrentInnerWindow();

  nsCOMPtr<nsIPermissionManager> permMgr =
    do_GetService(NS_PERMISSIONMANAGER_CONTRACTID);
  NS_ENSURE_TRUE(permMgr, NS_ERROR_UNEXPECTED);

  uint32_t permission = nsIPermissionManager::DENY_ACTION;
  nsresult rv =
    permMgr->TestPermissionFromWindow(aWindow, "telephony", &permission);
  NS_ENSURE_SUCCESS(rv, rv);

  if (permission != nsIPermissionManager::ALLOW_ACTION) {
    *aManager = nullptr;
    return NS_OK;
  }

  nsRefPtr<TelephonyManager> manager = TelephonyManager::Create(innerWindow);
  NS_ENSURE_TRUE(manager, NS_ERROR_UNEXPECTED);

  manager.forget(aManager);
  return NS_OK;
}
