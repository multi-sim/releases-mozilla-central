/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* Copyright 2012 Mozilla Foundation and Mozilla contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SystemWorkerManager.h"

#include "nsIObserverService.h"
#include "nsIJSContextStack.h"
#include "nsINetworkManager.h"
#include "nsIWifi.h"
#include "nsIWorkerHolder.h"
#include "nsIXPConnect.h"

#include "jsfriendapi.h"
#include "mozilla/dom/workers/Workers.h"
#ifdef MOZ_WIDGET_GONK
#include "mozilla/ipc/Netd.h"
#include "AutoMounter.h"
#include "TimeSetting.h"
#endif
#include "mozilla/ipc/Ril.h"
#include "nsContentUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
//TODO ifdef
#include "nsRadioInterfaceLayer.h"
#include "nsMSimRadioInterfaceLayer.h"
#include "WifiWorker.h"
#include "mozilla/StaticPtr.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOGD(args...)  __android_log_print(ANDROID_LOG_INFO, "Gonk", args)
#else
#define LOG(args...)  printf(args);
#endif

USING_WORKERS_NAMESPACE

using namespace mozilla::dom::gonk;
using namespace mozilla::ipc;
#ifdef MOZ_WIDGET_GONK
using namespace mozilla::system;
#endif

#define NS_NETWORKMANAGER_CID \
  { 0x33901e46, 0x33b8, 0x11e1, \
  { 0x98, 0x69, 0xf4, 0x6d, 0x04, 0xd2, 0x5b, 0xcc } }

const int INDEX_SIZE = 4;
const int DATA_SIZE = 4;
const int HEADER_SIZE = INDEX_SIZE + DATA_SIZE;

namespace {

NS_DEFINE_CID(kWifiWorkerCID, NS_WIFIWORKER_CID);
NS_DEFINE_CID(kNetworkManagerCID, NS_NETWORKMANAGER_CID);

// Doesn't carry a reference, we're owned by services.
SystemWorkerManager *gInstance = nullptr;

class ConnectWorkerToRIL : public WorkerTask
{
public:
  ConnectWorkerToRIL(int subId)
    : mSubId(subId)
  { }

  virtual bool RunTask(JSContext *aCx);
private:
  int mSubId;
};

//template <int index>
JSBool
PostToRIL(JSContext *cx, unsigned argc, jsval *vp)
{
  NS_ASSERTION(!NS_IsMainThread(), "Expecting to be on the worker thread");

  if (argc != 1) {
    JS_ReportError(cx, "Expecting a single argument with the RIL message");
    return false;
  }

  jsval v = JS_ARGV(cx, vp)[0];

  nsAutoPtr<RilRawData> rm(new RilRawData());
  JSAutoByteString abs;
  void *data;
  size_t size;
  if (JSVAL_IS_STRING(v)) {
    JSString *str = JSVAL_TO_STRING(v);
    if (!abs.encode(cx, str)) {
      return false;
    }

    size = JS_GetStringLength(str);
    data = abs.ptr();
  } else if (!JSVAL_IS_PRIMITIVE(v)) {
    JSObject *obj = JSVAL_TO_OBJECT(v);
    if (!JS_IsTypedArrayObject(obj, cx)) {
      JS_ReportError(cx, "Object passed in wasn't a typed array");
      return false;
    }

    uint32_t type = JS_GetTypedArrayType(obj, cx);
    if (type != js::ArrayBufferView::TYPE_INT8 &&
        type != js::ArrayBufferView::TYPE_UINT8 &&
        type != js::ArrayBufferView::TYPE_UINT8_CLAMPED) {
      JS_ReportError(cx, "Typed array data is not octets");
      return false;
    }

    size = JS_GetTypedArrayByteLength(obj, cx);
    data = JS_GetArrayBufferViewData(obj, cx);
  } else {
    JS_ReportError(cx,
                   "Incorrect argument. Expecting a string or a typed array");
    return false;
  }

  if (size > RilRawData::MAX_DATA_SIZE) {
    JS_ReportError(cx, "Passed-in data is too large");
    return false;
  }

  JSObject *workerGlobal = JS_GetGlobalObject(cx);
  jsval indexJs;
  JSBool ret = JS_GetProperty(cx, workerGlobal, "subscriptionId", &indexJs);
  int index = indexJs.toInt32();
  LOGD("XXX ret=%d, index=%d",ret, index);
  //TODO ignore requests from 1
//  if(index == 1) {
//    return true;
//  }
  //TODO See Ril.h, use RilProxyData
  rm->mSize = size + INDEX_SIZE;
  rm->mData[0] = (index >> 24) & 0xff;
  rm->mData[1] = (index >> 16) & 0xff;
  rm->mData[2] = (index >> 8) & 0xff;
  rm->mData[3] = index & 0xff;
  memcpy(&rm->mData[INDEX_SIZE], data, size);

  RilRawData *tosend = rm.forget();
  JS_ALWAYS_TRUE(SendRilRawData(&tosend));
  return true;
}

bool
ConnectWorkerToRIL::RunTask(JSContext *aCx)
{
  // Set up the postRILMessage on the function for worker -> RIL thread
  // communication.
  NS_ASSERTION(!NS_IsMainThread(), "Expecting to be on the worker thread");
  NS_ASSERTION(!JS_IsRunning(aCx), "Are we being called somehow?");
  JSObject *workerGlobal = JS_GetGlobalObject(aCx);
  bool ret = JS_DefineProperty(aCx, workerGlobal, "subscriptionId",
                               INT_TO_JSVAL(mSubId), nullptr, nullptr, 0);
  LOGD("ret = %d, mSubId=%d",ret, mSubId);
  return !!JS_DefineFunction(aCx, workerGlobal, "postRILMessage",
            PostToRIL, 1, 0);
  //TODO non-type param can only be constant expression
//  if (mSubId == 0) 
//    return !!JS_DefineFunction(aCx, workerGlobal, "postRILMessage",
//             PostToRIL<0>, 1, 0);
//  else
//    return !!JS_DefineFunction(aCx, workerGlobal, "postRILMessage",
//             PostToRIL<1>, 1, 0);
}

class RILReceiver : public RilConsumer
{
  class DispatchRILEvent : public WorkerTask
  {
  public:
    DispatchRILEvent(RilRawData *aMessage)
      : mMessage(aMessage)
    { }

    virtual bool RunTask(JSContext *aCx);

  private:
    nsAutoPtr<RilRawData> mMessage;
  };

public:
 RILReceiver(WorkerCrossThreadDispatcher *aDispatcher)
    : mDispatcher(aDispatcher)
  { }

  RILReceiver() { }

  virtual void MessageReceived(RilRawData *aMessage);
  void RegisterRILEvent(int index, WorkerCrossThreadDispatcher *aDispatcher);

private:
  nsRefPtr<WorkerCrossThreadDispatcher> mDispatcher;
  nsRefPtr<WorkerCrossThreadDispatcher> mDispatchers[2];
};

void
RILReceiver::MessageReceived(RilRawData *aMessage)
{
  int offset = 0,totalSize = aMessage->mSize;
  while (offset < totalSize) {
    unsigned int index, dataSize;
    //TODO See Ril.h, use RilProxyData
    index = aMessage->mData[offset + 0] << 24 |
            aMessage->mData[offset + 1] << 16 |
            aMessage->mData[offset + 2] << 8  |
            aMessage->mData[offset + 3];
    dataSize = aMessage->mData[offset + 4] << 24 |
               aMessage->mData[offset + 5] << 16 |
               aMessage->mData[offset + 6] << 8  |
               aMessage->mData[offset + 7];
    LOGD("XXX index=%d, dataSize=%d", index, dataSize);

    nsAutoPtr<RilRawData> data(new RilRawData());
    data->mSize = dataSize;
    memcpy(data->mData, &aMessage->mData[offset + HEADER_SIZE], dataSize);

    RilRawData *event = data.forget();
    nsRefPtr<DispatchRILEvent> dre(new DispatchRILEvent(event));

    mDispatchers[index]->PostTask(dre);

    offset += HEADER_SIZE + dataSize;
  }
}

void
RILReceiver::RegisterRILEvent(int index, WorkerCrossThreadDispatcher *aDispatcher)
{
  mDispatchers[index] = aDispatcher;
}

bool
RILReceiver::DispatchRILEvent::RunTask(JSContext *aCx)
{
  JSObject *obj = JS_GetGlobalObject(aCx);

  JSObject *array = JS_NewUint8Array(aCx, mMessage->mSize);
  if (!array) {
    return false;
  }

  memcpy(JS_GetArrayBufferViewData(array, aCx), mMessage->mData, mMessage->mSize);
  jsval argv[] = { OBJECT_TO_JSVAL(array) };
  return JS_CallFunctionName(aCx, obj, "onRILMessage", NS_ARRAY_LENGTH(argv),
                             argv, argv);
}

#ifdef MOZ_WIDGET_GONK

JSBool
DoNetdCommand(JSContext *cx, unsigned argc, jsval *vp)
{
  NS_ASSERTION(!NS_IsMainThread(), "Expecting to be on the worker thread");

  if (argc != 1) {
    JS_ReportError(cx, "Expecting a single argument with the Netd message");
    return false;
  }

  jsval v = JS_ARGV(cx, vp)[0];

  JSAutoByteString abs;
  void *data;
  size_t size;
  if (JSVAL_IS_STRING(v)) {
    JSString *str = JSVAL_TO_STRING(v);
    if (!abs.encode(cx, str)) {
      return false;
    }

    size = JS_GetStringLength(str);
    if (!size) {
      JS_ReportError(cx, "Command length is zero");
      return false;
    }

    data = abs.ptr();
    if (!data) {
      JS_ReportError(cx, "Command string is empty");
      return false;
    }
  } else if (!JSVAL_IS_PRIMITIVE(v)) {
    JSObject *obj = JSVAL_TO_OBJECT(v);
    if (!JS_IsTypedArrayObject(obj, cx)) {
      JS_ReportError(cx, "Object passed in wasn't a typed array");
      return false;
    }

    uint32_t type = JS_GetTypedArrayType(obj, cx);
    if (type != js::ArrayBufferView::TYPE_INT8 &&
        type != js::ArrayBufferView::TYPE_UINT8 &&
        type != js::ArrayBufferView::TYPE_UINT8_CLAMPED) {
      JS_ReportError(cx, "Typed array data is not octets");
      return false;
    }

    size = JS_GetTypedArrayByteLength(obj, cx);
    if (!size) {
      JS_ReportError(cx, "Typed array byte length is zero");
      return false;
    }

    data = JS_GetArrayBufferViewData(obj, cx);
    if (!data) {
      JS_ReportError(cx, "Array buffer view data is NULL");
      return false;
    }
  } else {
    JS_ReportError(cx,
                   "Incorrect argument. Expecting a string or a typed array");
    return false;
  }

  // Reserve one space for '\0'.
  if (size > MAX_COMMAND_SIZE - 1 || size <= 0) {
    JS_ReportError(cx, "Passed-in data size is invalid");
    return false;
  }

  NetdCommand* command = new NetdCommand();

  memcpy(command->mData, data, size);
  // Include the null terminate to the command to make netd happy.
  command->mData[size] = 0;
  command->mSize = size + 1;
  SendNetdCommand(command);
  return true;
}

class ConnectWorkerToNetd : public WorkerTask
{
public:
  virtual bool RunTask(JSContext *aCx);
};

bool
ConnectWorkerToNetd::RunTask(JSContext *aCx)
{
  // Set up the DoNetdCommand on the function for worker <--> Netd process
  // communication.
  NS_ASSERTION(!NS_IsMainThread(), "Expecting to be on the worker thread");
  NS_ASSERTION(!JS_IsRunning(aCx), "Are we being called somehow?");
  JSObject *workerGlobal = JS_GetGlobalObject(aCx);
  return !!JS_DefineFunction(aCx, workerGlobal, "postNetdCommand",
                             DoNetdCommand, 1, 0);
}

class NetdReceiver : public NetdConsumer
{
  class DispatchNetdEvent : public WorkerTask
  {
  public:
    DispatchNetdEvent(NetdCommand *aMessage)
      : mMessage(aMessage)
    { }

    virtual bool RunTask(JSContext *aCx);

  private:
    nsAutoPtr<NetdCommand> mMessage;
  };

public:
  NetdReceiver(WorkerCrossThreadDispatcher *aDispatcher)
    : mDispatcher(aDispatcher)
  { }

  virtual void MessageReceived(NetdCommand *aMessage) {
    nsRefPtr<DispatchNetdEvent> dre(new DispatchNetdEvent(aMessage));
    if (!mDispatcher->PostTask(dre)) {
      NS_WARNING("Failed to PostTask to net worker");
    }
  }

private:
  nsRefPtr<WorkerCrossThreadDispatcher> mDispatcher;
};

bool
NetdReceiver::DispatchNetdEvent::RunTask(JSContext *aCx)
{
  JSObject *obj = JS_GetGlobalObject(aCx);

  JSObject *array = JS_NewUint8Array(aCx, mMessage->mSize);
  if (!array) {
    return false;
  }

  memcpy(JS_GetUint8ArrayData(array, aCx), mMessage->mData, mMessage->mSize);
  jsval argv[] = { OBJECT_TO_JSVAL(array) };
  return JS_CallFunctionName(aCx, obj, "onNetdMessage", NS_ARRAY_LENGTH(argv),
                             argv, argv);
}

#endif // MOZ_WIDGET_GONK

} // anonymous namespace

SystemWorkerManager::SystemWorkerManager()
  : mShutdown(false)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(!gInstance, "There should only be one instance!");
}

SystemWorkerManager::~SystemWorkerManager()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(!gInstance || gInstance == this,
               "There should only be one instance!");
  gInstance = nullptr;
}

#ifdef MOZ_WIDGET_GONK
static mozilla::StaticRefPtr<TimeSetting> sTimeSetting;
#endif

nsresult
SystemWorkerManager::Init()
{
  if (XRE_GetProcessType() != GeckoProcessType_Default) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_ASSERTION(NS_IsMainThread(), "We can only initialize on the main thread");
  NS_ASSERTION(!mShutdown, "Already shutdown!");

  JSContext* cx = nsContentUtils::ThreadJSContextStack()->GetSafeJSContext();
  NS_ENSURE_TRUE(cx, NS_ERROR_FAILURE);

  nsCxPusher pusher;
  if (!pusher.Push(cx, false)) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = InitMSimRIL(cx);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to initialize Multi-SIM RIL/Telephony!");
    return rv;
  }

//  nsresult rv = InitRIL(cx);
//  if (NS_FAILED(rv)) {
//    NS_WARNING("Failed to initialize RIL/Telephony!");
//    return rv;
//  }

  rv = InitWifi(cx);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to initialize WiFi Networking!");
    return rv;
  }

#ifdef MOZ_WIDGET_GONK
  InitAutoMounter();
  sTimeSetting = new TimeSetting();
  rv = InitNetd(cx);
  NS_ENSURE_SUCCESS(rv, rv);
#endif

  nsCOMPtr<nsIObserverService> obs =
    do_GetService(NS_OBSERVERSERVICE_CONTRACTID);
  if (!obs) {
    NS_WARNING("Failed to get observer service!");
    return NS_ERROR_FAILURE;
  }

  rv = obs->AddObserver(this, WORKERS_SHUTDOWN_TOPIC, false);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to initialize worker shutdown event!");
    return rv;
  }

  return NS_OK;
}

void
SystemWorkerManager::Shutdown()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  mShutdown = true;

#ifdef MOZ_WIDGET_GONK
  ShutdownAutoMounter();
  sTimeSetting = nullptr;
#endif

  StopRil();

  mRIL = nullptr;
  mMSimRIL = nullptr;

#ifdef MOZ_WIDGET_GONK
  StopNetd();
  mNetdWorker = nullptr;
#endif

  nsCOMPtr<nsIWifi> wifi(do_QueryInterface(mWifiWorker));
  if (wifi) {
    wifi->Shutdown();
    wifi = nullptr;
  }
  mWifiWorker = nullptr;

  nsCOMPtr<nsIObserverService> obs =
    do_GetService(NS_OBSERVERSERVICE_CONTRACTID);
  if (obs) {
    obs->RemoveObserver(this, WORKERS_SHUTDOWN_TOPIC);
  }
}

// static
already_AddRefed<SystemWorkerManager>
SystemWorkerManager::FactoryCreate()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsRefPtr<SystemWorkerManager> instance(gInstance);

  if (!instance) {
    instance = new SystemWorkerManager();
    if (NS_FAILED(instance->Init())) {
      instance->Shutdown();
      return nullptr;
    }

    gInstance = instance;
  }

  return instance.forget();
}

// static
nsIInterfaceRequestor*
SystemWorkerManager::GetInterfaceRequestor()
{
  return gInstance;
}

NS_IMETHODIMP
SystemWorkerManager::GetInterface(const nsIID &aIID, void **aResult)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (aIID.Equals(NS_GET_IID(nsIRadioInterfaceLayer))) {
    NS_IF_ADDREF(*reinterpret_cast<nsIRadioInterfaceLayer**>(aResult) = mRIL);
    return NS_OK;
  }

  //TODO nsIMSimRadioInterfaceLayer

  if (aIID.Equals(NS_GET_IID(nsIWifi))) {
    return CallQueryInterface(mWifiWorker,
                              reinterpret_cast<nsIWifi**>(aResult));
  }

#ifdef MOZ_WIDGET_GONK
  if (aIID.Equals(NS_GET_IID(nsINetworkManager))) {
    return CallQueryInterface(mNetdWorker,
                              reinterpret_cast<nsINetworkManager**>(aResult));
  }
#endif

  NS_WARNING("Got nothing for the requested IID!");
  return NS_ERROR_NO_INTERFACE;
}

#if 0
nsresult
SystemWorkerManager::InitRIL(JSContext *cx)
{
  // We're keeping as much of this implementation as possible in JS, so the real
  // worker lives in RadioInterfaceLayer.js. All we do here is hold it alive and
  // hook it up to the RIL thread.
  nsCOMPtr<nsIRadioInterfaceLayer> ril = do_CreateInstance("@mozilla.org/ril;1");
  NS_ENSURE_TRUE(ril, NS_ERROR_FAILURE);

  nsCOMPtr<nsIWorkerHolder> worker = do_QueryInterface(ril);
  if (worker) {
    jsval workerval;
    nsresult rv = worker->GetWorker(&workerval);
    NS_ENSURE_SUCCESS(rv, rv);

    NS_ENSURE_TRUE(!JSVAL_IS_PRIMITIVE(workerval), NS_ERROR_UNEXPECTED);

    JSAutoRequest ar(cx);
    JSAutoCompartment ac(cx, JSVAL_TO_OBJECT(workerval));

    WorkerCrossThreadDispatcher *wctd =
      GetWorkerCrossThreadDispatcher(cx, workerval);
    if (!wctd) {
      return NS_ERROR_FAILURE;
    }

    nsRefPtr<ConnectWorkerToRIL> connection = new ConnectWorkerToRIL();
    if (!wctd->PostTask(connection)) {
      return NS_ERROR_UNEXPECTED;
    }

    // Now that we're set up, connect ourselves to the RIL thread.
    mozilla::RefPtr<RILReceiver> receiver = new RILReceiver(wctd);
    StartRil(receiver);
  }

  mRIL = ril;
  return NS_OK;
}
#endif

nsresult
SystemWorkerManager::InitMSimRIL(JSContext *cx)
{
  nsCOMPtr<nsIMSimRadioInterfaceLayer> mSimRIL = do_CreateInstance("@mozilla.org/msim_ril;1");
  NS_ENSURE_TRUE(mSimRIL, NS_ERROR_FAILURE);

  mozilla::RefPtr<RILReceiver> receiver = new RILReceiver();

  for (int i = 0; i < 2; i++) {
    nsCOMPtr<nsIMSimWorkerHolder> workers = do_QueryInterface(mSimRIL);
    if (!workers) {
      break;
    }

    jsval workerval;
    nsresult rv = workers->GetWorker(i, &workerval);
    NS_ENSURE_SUCCESS(rv, rv);

    NS_ENSURE_TRUE(!JSVAL_IS_PRIMITIVE(workerval), NS_ERROR_UNEXPECTED);

    JSAutoRequest ar(cx);
    JSAutoCompartment ac(cx, JSVAL_TO_OBJECT(workerval));

    WorkerCrossThreadDispatcher *wctd =
      GetWorkerCrossThreadDispatcher(cx, workerval);
    if (!wctd) {
      return NS_ERROR_FAILURE;
    }

    // TODO race?
    nsRefPtr<ConnectWorkerToRIL> connection = new ConnectWorkerToRIL(i);
    if (!wctd->PostTask(connection)) {
      return NS_ERROR_UNEXPECTED;
    }

    receiver->RegisterRILEvent(i, wctd);
    
  }
  // Now that we're set up, connect ourselves to the RIL thread.
  StartRil(receiver);
  mMSimRIL = mSimRIL ;
  return NS_OK;
}

#ifdef MOZ_WIDGET_GONK
nsresult
SystemWorkerManager::InitNetd(JSContext *cx)
{
  nsCOMPtr<nsIWorkerHolder> worker = do_GetService(kNetworkManagerCID);
  NS_ENSURE_TRUE(worker, NS_ERROR_FAILURE);

  jsval workerval;
  nsresult rv = worker->GetWorker(&workerval);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(!JSVAL_IS_PRIMITIVE(workerval), NS_ERROR_UNEXPECTED);

  JSAutoRequest ar(cx);
  JSAutoCompartment ac(cx, JSVAL_TO_OBJECT(workerval));

  WorkerCrossThreadDispatcher *wctd =
    GetWorkerCrossThreadDispatcher(cx, workerval);
  if (!wctd) {
    NS_WARNING("Failed to GetWorkerCrossThreadDispatcher");
    return NS_ERROR_FAILURE;
  }

  nsRefPtr<ConnectWorkerToNetd> connection = new ConnectWorkerToNetd();
  if (!wctd->PostTask(connection)) {
    NS_WARNING("Failed to connect worker to netd");
    return NS_ERROR_UNEXPECTED;
  }

  // Now that we're set up, connect ourselves to the Netd process.
  mozilla::RefPtr<NetdReceiver> receiver = new NetdReceiver(wctd);
  StartNetd(receiver);
  mNetdWorker = worker;
  return NS_OK;
}
#endif

nsresult
SystemWorkerManager::InitWifi(JSContext *cx)
{
  nsCOMPtr<nsIWorkerHolder> worker = do_CreateInstance(kWifiWorkerCID);
  NS_ENSURE_TRUE(worker, NS_ERROR_FAILURE);

  mWifiWorker = worker;
  return NS_OK;
}

NS_IMPL_ISUPPORTS2(SystemWorkerManager, nsIObserver, nsIInterfaceRequestor)

NS_IMETHODIMP
SystemWorkerManager::Observe(nsISupports *aSubject, const char *aTopic,
                             const PRUnichar *aData)
{
  if (!strcmp(aTopic, WORKERS_SHUTDOWN_TOPIC)) {
    Shutdown();
  }

  return NS_OK;
}
