/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=40: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_telephony_telephonymanager_h__
#define mozilla_dom_telephony_telephonymanager_h__

#include "TelephonyCommon.h"

#include "nsIDOMTelephonyManager.h"
#include "nsIDOMTelephony.h"
#include "nsIDOMTelephonyCall.h"
#include "nsIRadioInterfaceLayer.h"
#include "Telephony.h"
#include "TelephonyCall.h"

class nsIScriptContext;
class nsPIDOMWindow;

BEGIN_TELEPHONY_NAMESPACE

class TelephonyManager : public nsDOMEventTargetHelper,
                         public nsIDOMTelephonyManager
{
  nsTArray<nsRefPtr<Telephony> > mPhones;

  nsTArray<nsRefPtr<TelephonyCall> > mCalls;
  nsCOMPtr<nsIRILContentHelper> mRIL;

  nsCOMPtr<nsIRILTelephonyManagerCallback> mRILTelephonyManagerCallback;

  nsString mState;

  // Cached calls array object. Cleared whenever mCalls changes and then rebuilt
  // once a page looks for the liveCalls attribute.
  JSObject* mCallsArray;

  bool mRooted;

public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMTELEPHONYMANAGER
  NS_DECL_NSIRILTELEPHONYMANAGERCALLBACK
  NS_FORWARD_NSIDOMEVENTTARGET(nsDOMEventTargetHelper::)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(
                                                   TelephonyManager,
                                                   nsDOMEventTargetHelper)

  nsIDOMEventTarget*
  ToIDOMEventTarget() const
  {
    return static_cast<nsDOMEventTargetHelper*>(
             const_cast<TelephonyManager*>(this));
  }

  static already_AddRefed<TelephonyManager>
  Create(nsPIDOMWindow* aOwner);


  nsISupports*
  ToISupports() const
  {
    return ToIDOMEventTarget();
  }

  void
  AddCall(TelephonyCall* aCall)
  {
    NS_ASSERTION(!mCalls.Contains(aCall), "Already know about this one!");
    mCalls.AppendElement(aCall);
    mCallsArray = nullptr;
    NotifyCallsChanged(aCall);
  }

  void
  RemoveCall(TelephonyCall* aCall)
  {
    NS_ASSERTION(mCalls.Contains(aCall), "Didn't know about this one!");
    mCalls.RemoveElement(aCall);
    mCallsArray = nullptr;
    NotifyCallsChanged(aCall);
  }

private:
  TelephonyManager();
  ~TelephonyManager();

  nsresult
  NotifyCallsChanged(TelephonyCall* aCall);

  class RILTelephonyManagerCallback : public nsIRILTelephonyManagerCallback
  {
    TelephonyManager* mTelephonyManager;

  public:
    NS_DECL_ISUPPORTS
    NS_FORWARD_NSIRILTELEPHONYMANAGERCALLBACK(mTelephonyManager->)

    RILTelephonyManagerCallback(TelephonyManager* aTelephonyManager)
    : mTelephonyManager(aTelephonyManager)
    {
      NS_ASSERTION(mTelephonyManager, "Null pointer!");
    }
  };
};

END_TELEPHONY_NAMESPACE

#endif // mozilla_dom_telephony_telephonymanager_h__
