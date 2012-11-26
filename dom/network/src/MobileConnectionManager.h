/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_network_MobileConnectionManager_h
#define mozilla_dom_network_MobileConnectionManager_h

#include "nsDOMEventTargetHelper.h"
#include "nsIDOMMobileConnectionManager.h"
#include "nsCycleCollectionParticipant.h"
#include "MobileConnection.h"

class nsPIDOMWindow;

namespace mozilla {
namespace dom {
namespace network {

/* Header file */
class MobileConnectionManager 
: public nsIDOMMozMobileConnectionManager
 ,public nsDOMEventTargetHelper
{
  nsTArray<nsRefPtr<MobileConnection> > mMobileConnections;
  bool mRooted;
  JSObject* jsMobileConnections;

public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMMOZMOBILECONNECTIONMANAGER

  NS_FORWARD_NSIDOMEVENTTARGET(nsDOMEventTargetHelper::)

  MobileConnectionManager();

  already_AddRefed<MobileConnectionManager> Init(nsPIDOMWindow *aWindow);
  void Shutdown();

  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(MobileConnectionManager,
                                                         nsDOMEventTargetHelper)



private:
  ~MobileConnectionManager();

protected:
  /* additional members */

};

} // namespace network
} // namespace dom
} // namespace mozilla

#endif /* mozilla_dom_network_MobileConnection_h */
