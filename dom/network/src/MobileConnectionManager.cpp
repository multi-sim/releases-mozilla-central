/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MobileConnectionManager.h"
#include "nsIDOMClassInfo.h"
#include "nsContentUtils.h"
#include "nsTArrayHelpers.h"


#define PHONE_NUMBER 2

DOMCI_DATA(MozMobileConnectionManager,mozilla::dom::network::MobileConnectionManager)

namespace mozilla {
namespace dom {
namespace network {

NS_IMPL_CYCLE_COLLECTION_CLASS(MobileConnectionManager)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(MobileConnectionManager,
                                                  nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(MobileConnectionManager,
                                               nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(jsMobileConnections)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(MobileConnectionManager,
                                                nsDOMEventTargetHelper)
  tmp->mMobileConnections.Clear();
  tmp->jsMobileConnections = nullptr;

NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(MobileConnectionManager)
  NS_INTERFACE_MAP_ENTRY(nsIDOMMozMobileConnectionManager)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMMozMobileConnectionManager)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(MozMobileConnectionManager)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(MobileConnectionManager, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(MobileConnectionManager, nsDOMEventTargetHelper)

MobileConnectionManager::MobileConnectionManager()
: mRooted(false),
  jsMobileConnections(nullptr)
{
  /* member initializers and constructor code */
}

MobileConnectionManager::~MobileConnectionManager()
{
  /* destructor code */
  if (mRooted) {
    NS_DROP_JS_OBJECTS(this, MobileConnectionManager);
  }
}
already_AddRefed<MobileConnectionManager> 
MobileConnectionManager::Init(nsPIDOMWindow* aOwner)
{
  BindToOwner(aOwner);
  for (uint32_t index = 0; index < PHONE_NUMBER; index++) {
    nsRefPtr<MobileConnection> aMobileConnection = new MobileConnection(index);
    NS_ENSURE_TRUE(aMobileConnection, nullptr);      
    aMobileConnection->Init(aOwner);
    mMobileConnections.AppendElement(aMobileConnection);
  }
  return this;
}

void MobileConnectionManager::Shutdown()
{
    for (uint32_t index = 0; index < PHONE_NUMBER; index++) {
      mMobileConnections[index]->Shutdown();
      mMobileConnections[index] = nullptr;
    }
    return;
}


/* readonly attribute jsval mobileConnections; */
NS_IMETHODIMP 
MobileConnectionManager::GetMobileConnections(JS::Value *aMobileConnections)
{
  if (!jsMobileConnections) {
    nsresult rv;
    nsIScriptContext* sc = GetContextForEventHandlers(&rv);
    NS_ENSURE_SUCCESS(rv, rv);
    if (sc) {
      rv = nsTArrayToJSArray(sc->GetNativeContext(), mMobileConnections, 
                             &jsMobileConnections);
      NS_ENSURE_SUCCESS(rv, rv);

      if (!mRooted) {
        NS_HOLD_JS_OBJECTS(this, MobileConnectionManager);
        mRooted = true;
      }
    } else {
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  aMobileConnections->setObject(*jsMobileConnections);
  return NS_OK;
}

/* readonly attribute nsIDOMMozMobileConnection defaultMobileConnection; */
NS_IMETHODIMP 
MobileConnectionManager::GetDefaultMobileConnection(nsIDOMMozMobileConnection * *aDefaultMobileConnection)
{
    *aDefaultMobileConnection = mMobileConnections[0];
    return NS_OK;
}
} // namespace network
} // namespace dom
} // namespace mozilla
