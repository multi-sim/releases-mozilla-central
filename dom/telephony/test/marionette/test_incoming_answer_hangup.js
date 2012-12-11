/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 10000;

SpecialPowers.addPermission("telephony", true, document);

let mgr = window.navigator.mozTelephonyManager;
let activeTelephony = mgr.defaultPhone;
let idleTelephony = mgr.phones[1];
let number = "5555552368";
let incoming;
let is2ndTest = false;
let pendingEmulatorCmdCount = 0;

function verifyInitialState() {
  log("Verifying initial state.");
  ok(mgr);
  ok(activeTelephony);
  ok(idleTelephony);
  is(activeTelephony.active, null);
  is(idleTelephony.active, null);
  ok(activeTelephony.calls);
  ok(idleTelephony.calls);
  is(activeTelephony.calls.length, 0);
  is(idleTelephony.calls.length, 0);

  pendingEmulatorCmdCount++;
  runEmulatorCmd("gsm list", function(result) {
    log("Initial call list: " + result);
    pendingEmulatorCmdCount--;

    is(result[0], "OK");
    simulateIncoming();
  });
}

function simulateIncoming() {
  log("Simulating an incoming call.");

  activeTelephony.onincoming = function onincoming(event) {
    log("Received 'incoming' call event.");
    incoming = event.call;
    ok(incoming);
    is(incoming.number, number);
    is(incoming.state, "incoming");

    is(mgr.calls.length, 1);
    is(activeTelephony.calls.length, 1);
    is(activeTelephony.calls[0], incoming);
    is(idleTelephony.calls.length, 0);

    pendingEmulatorCmdCount++;
    runEmulatorCmd("gsm list", function(result) {
      log("Call list is now: " + result);
      pendingEmulatorCmdCount--;

      is(result[0], "inbound from " + number + " : incoming");
      is(result[1], "OK");
      answer();
    });
  };
  runEmulatorCmd("gsm call " + number);
}

function answer() {
  log("Answering the incoming call.");

  let gotConnecting = false;
  incoming.onconnecting = function onconnecting(event) { 
    log("Received 'connecting' call event.");
    is(incoming, event.call);
    is(incoming.state, "connecting");
    gotConnecting = true;
  };

  incoming.onconnected = function onconnected(event) {
    log("Received 'connected' call event.");
    is(incoming, event.call);
    is(incoming.state, "connected");
    ok(gotConnecting);

    is(incoming, activeTelephony.active);

    pendingEmulatorCmdCount++;
    runEmulatorCmd("gsm list", function(result) {
      log("Call list is now: " + result);
      pendingEmulatorCmdCount--;

      is(result[0], "inbound from " + number + " : active");
      is(result[1], "OK");
      hangUp();
    });
  };
  incoming.answer();
};

function hangUp() {
  log("Hanging up the incoming call.");

  let gotDisconnecting = false;
  incoming.ondisconnecting = function ondisconnecting(event) {
    log("Received 'disconnecting' call event.");
    is(incoming, event.call);
    is(incoming.state, "disconnecting");
    gotDisconnecting = true;
  };

  incoming.ondisconnected = function ondisconnected(event) {
    log("Received 'disconnected' call event.");
    is(incoming, event.call);
    is(incoming.state, "disconnected");
    ok(gotDisconnecting);

    is(activeTelephony.active, null);
    is(activeTelephony.calls.length, 0);
    is(idleTelephony.calls.length, 0);
    is(mgr.calls.length, 0);

    pendingEmulatorCmdCount++;
    runEmulatorCmd("gsm list", function(result) {
      log("Call list is now: " + result);
      pendingEmulatorCmdCount--;

      is(result[0], "OK");
      if (is2ndTest) {
        cleanUp();
      } else {
        runNextTest();
      }
    });
  };
  incoming.hangUp();
}

function runNextTest() {
  log("Run next test: using modem 1");
  is2ndTest = true;

  pendingEmulatorCmdCount++;
  runEmulatorCmd("mux select 1", function(result) {
    pendingEmulatorCmdCount--;
    is(result[0], "OK");

    activeTelephony = mgr.phones[1];
    idleTelephony = mgr.phones[0];
  });

  ok(activeTelephony);
  ok(idleTelephony);
  is(activeTelephony.active, null);
  is(idleTelephony.active, null);
  ok(activeTelephony.calls);
  ok(idleTelephony.calls);
  is(mgr.calls.length, 0);
  is(activeTelephony.calls.length, 0);
  is(idleTelephony.calls.length, 0);

  pendingEmulatorCmdCount++;
  runEmulatorCmd("gsm list", function(result) {
    log("Initial call list: " + result);
    pendingEmulatorCmdCount--;
    is(result[0], "OK");
    simulateIncoming();
  });
}

function cleanUp() {
  if (pendingEmulatorCmdCount > 0) {
    window.setTimeout(cleanUp, 100);
    return;
  }
  SpecialPowers.removePermission("telephony", document);
  finish();
}

verifyInitialState();
